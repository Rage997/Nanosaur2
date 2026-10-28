#ifdef __EMSCRIPTEN__
#define WEBGL_COMPAT_IMPLEMENTATION
#include "game.h"
#include <stddef.h>
#include <string.h>

#define MATRIX_DEPTH 32
#define LIGHT_COUNT 8
#define ATTRIBUTE_COUNT 5

typedef struct
{
	GLfloat value[MATRIX_DEPTH][16];
	int top;
} MatrixStack;
typedef struct
{
	const void *pointer;
	GLint size;
	GLboolean enabled;
} ClientArray;
typedef struct
{
	GLuint name;
	size_t capacity;
} Buffer;
typedef struct
{
	GLfloat position[3], color[4], uv[2];
} Vertex;
static MatrixStack model, projection, textureMatrix[2];
static GLenum matrixMode = GL_MODELVIEW;
static unsigned activeTexture, clientTexture;
static GLboolean lighting, fog, normalizeNormals, rescaleNormals, alphaTest, colorMaterial, twoSided;
static GLboolean textured[2], texgen[2][2], lights[LIGHT_COUNT];
// All game depth-mask writes go through this backend. Reading our owned state
// avoids a synchronous getParameter round trip for every OGL_PushState.
static GLboolean depthWriteMask = GL_TRUE;
static GLfloat color[4] = {1, 1, 1, 1}, uv[2], material[4] = {1, 1, 1, 1};
static GLfloat ambient[4] = {.2f, .2f, .2f, 1}, lightPosition[LIGHT_COUNT][4];
static GLfloat lightDiffuse[LIGHT_COUNT][4], lightAmbient[LIGHT_COUNT][4];
static GLfloat fogColor[4], fogStart, fogEnd = 1, fogDensity = 1, alphaReference;
static GLint fogMode = GL_EXP, alphaFunction = GL_ALWAYS;
static GLint envMode[2] = {GL_MODULATE, GL_MODULATE};
static GLint combineRGB[2] = {GL_MODULATE, GL_MODULATE}, combineAlpha[2] = {GL_MODULATE, GL_MODULATE};
static ClientArray arrays[ATTRIBUTE_COUNT];
static GLenum primitive;
static GLboolean wireframe;
static Vertex *vertices;
static size_t vertexCount, vertexCapacity;
static uint32_t *indices;
static size_t indexCapacity, quadCount;
static unsigned char *convertedPixels;
static size_t pixelCapacity;
static GLuint program, vao, whiteTexture;
static Buffer attributes[ATTRIBUTE_COUNT], elementBuffer, immediateBuffer, quadBuffer;
static bool dirty = true;
static struct
{
	GLint model, projection, normal, texture[2], lighting, normalize, material, colorMaterial;
	GLint ambient, lightPosition, lightDiffuse, lightAmbient, lights, twoSided;
	GLint textured, texgen, combine, fog, fogColor, fogRange, fogDensity, fogMode, alphaTest, alphaFunction,
		alphaReference;
} u;

static const char *vertexSource =
	"#version 300 es\nprecision highp float;\n"
	"layout(location=0) in vec3 position; layout(location=1) in vec3 normal;\n"
	"layout(location=2) in vec4 color; layout(location=3) in vec2 uv0; layout(location=4) in vec2 uv1;\n"
	"uniform mat4 model, projection, texture0, texture1; uniform mat3 normalMatrix;\n"
	"uniform bool lighting, normalizeNormals, colorMaterial, twoSided; uniform vec4 material, ambient;\n"
	"uniform vec4 lightPosition[8], lightDiffuse[8], lightAmbient[8]; uniform int lights[8];\n"
	"uniform ivec4 texgen; out vec4 frontColor, backColor; out vec2 tex0, tex1; out float fogDistance;\n"
	"vec4 shade(vec3 n,vec3 eye,vec4 c){ vec3 sum=ambient.rgb;\n"
	"for(int i=0;i<8;i++){if(lights[i]!=0){vec3 d=normalize(lightPosition[i].xyz-eye*lightPosition[i].w);\n"
	"sum+=lightAmbient[i].rgb+lightDiffuse[i].rgb*max(dot(n,d),0.0);}} return vec4(clamp(sum*c.rgb,0.0,1.0),c.a);}\n"
	"void main(){vec4 eye=model*vec4(position,1.0); gl_Position=projection*eye;\n"
	"vec3 n=normalMatrix*normal; if(normalizeNormals)n=normalize(n);\n"
	"vec4 c=colorMaterial?color:material; frontColor=lighting?shade(n,eye.xyz,c):color;\n"
	"backColor=lighting&&twoSided?shade(-n,eye.xyz,c):frontColor;\n"
	"vec2 a=uv0,b=uv1; if(any(notEqual(texgen,ivec4(0)))){\n"
	"vec3 r=reflect(normalize(eye.xyz),normalize(n)); vec2 s=r.xy/max(2.0*length(vec3(r.xy,r.z+1.0)),0.00001)+0.5;\n"
	"if(texgen.x!=0)a.x=s.x; if(texgen.y!=0)a.y=s.y; if(texgen.z!=0)b.x=s.x; if(texgen.w!=0)b.y=s.y;}\n"
	"tex0=(texture0*vec4(a,0,1)).xy; tex1=(texture1*vec4(b,0,1)).xy; fogDistance=abs(eye.z);}\n";
static const char *fragmentSource =
	"#version 300 es\nprecision highp float;\n"
	"in vec4 frontColor,backColor; in vec2 tex0,tex1; in float fogDistance; out vec4 outputColor;\n"
	"uniform sampler2D image0,image1; uniform ivec2 textured; uniform ivec4 combine;\n"
	"uniform bool fog,alphaTest; uniform vec4 fogColor; uniform vec2 fogRange;\n"
	"uniform float fogDensity,alphaReference; uniform int fogMode,alphaFunction;\n"
	"vec4 stage(vec4 c,vec4 t,int rgb,int a){return clamp(vec4(rgb==1?c.rgb+t.rgb:c.rgb*t.rgb,a==1?c.a+t.a:c.a*t.a),0.0,1.0);}\n"
	"void main(){vec4 c=gl_FrontFacing?frontColor:backColor;\n"
	"if(textured.x!=0)c=stage(c,texture(image0,tex0),combine.x,combine.y);\n"
	"if(textured.y!=0)c=stage(c,texture(image1,tex1),combine.z,combine.w);\n"
	"if(alphaTest){bool pass=true; if(alphaFunction==512)pass=false;\n"
	"else if(alphaFunction==513)pass=c.a<alphaReference; else if(alphaFunction==514)pass=c.a==alphaReference;\n"
	"else if(alphaFunction==515)pass=c.a<=alphaReference; else if(alphaFunction==516)pass=c.a>alphaReference;\n"
	"else if(alphaFunction==517)pass=c.a!=alphaReference; else if(alphaFunction==518)pass=c.a>=alphaReference; if(!pass)discard;}\n"
	"if(fog){float f; if(fogMode==9729)f=(fogRange.y-fogDistance)/max(fogRange.y-fogRange.x,0.00001);\n"
	"else if(fogMode==2049){float d=fogDensity*fogDistance; f=exp(-d*d);}else f=exp(-fogDensity*fogDistance);\n"
	"c.rgb=mix(fogColor.rgb,c.rgb,clamp(f,0.0,1.0));} outputColor=c;}\n";

static void Identity(GLfloat *m)
{
	memset(m, 0, 16 * sizeof(*m));
	m[0] = m[5] = m[10] = m[15] = 1;
}
static MatrixStack *CurrentStack(void)
{
	switch (matrixMode)
	{
	case GL_MODELVIEW:
		return &model;
	case GL_PROJECTION:
		return &projection;
	case GL_TEXTURE:
		return &textureMatrix[activeTexture];
	default:
		DoFatalAlert("Unsupported matrix mode: %x", matrixMode);
		return &model;
	}
}
static GLuint Compile(GLenum type, const char *source)
{
	GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, &source, NULL);
	glCompileShader(shader);
	GLint success;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
	if (!success)
	{
		char message[2048];
		glGetShaderInfoLog(shader, sizeof(message), NULL, message);
		DoFatalAlert("WebGL shader compilation failed: %s", message);
	}
	return shader;
}
void WebGL_Init(void)
{
	Identity(model.value[0]);
	Identity(projection.value[0]);
	Identity(textureMatrix[0].value[0]);
	Identity(textureMatrix[1].value[0]);
	depthWriteMask = GL_TRUE;
	glDepthMask(depthWriteMask);
	GLuint vs = Compile(GL_VERTEX_SHADER, vertexSource), fs = Compile(GL_FRAGMENT_SHADER, fragmentSource);
	program = glCreateProgram();
	glAttachShader(program, vs);
	glAttachShader(program, fs);
	glLinkProgram(program);
	glDeleteShader(vs);
	glDeleteShader(fs);
	GLint success;
	glGetProgramiv(program, GL_LINK_STATUS, &success);
	if (!success)
	{
		char message[2048];
		glGetProgramInfoLog(program, sizeof(message), NULL, message);
		DoFatalAlert("WebGL shader link failed: %s", message);
	}
	glUseProgram(program);
#define UNIFORM(field, name) u.field = glGetUniformLocation(program, name)
	UNIFORM(model, "model");
	UNIFORM(projection, "projection");
	UNIFORM(normal, "normalMatrix");
	UNIFORM(texture[0], "texture0");
	UNIFORM(texture[1], "texture1");
	UNIFORM(lighting, "lighting");
	UNIFORM(normalize, "normalizeNormals");
	UNIFORM(material, "material");
	UNIFORM(colorMaterial, "colorMaterial");
	UNIFORM(ambient, "ambient");
	UNIFORM(twoSided, "twoSided");
	UNIFORM(lightPosition, "lightPosition[0]");
	UNIFORM(lightDiffuse, "lightDiffuse[0]");
	UNIFORM(lightAmbient, "lightAmbient[0]");
	UNIFORM(lights, "lights[0]");
	UNIFORM(textured, "textured");
	UNIFORM(texgen, "texgen");
	UNIFORM(combine, "combine");
	UNIFORM(fog, "fog");
	UNIFORM(fogColor, "fogColor");
	UNIFORM(fogRange, "fogRange");
	UNIFORM(fogDensity, "fogDensity");
	UNIFORM(fogMode, "fogMode");
	UNIFORM(alphaTest, "alphaTest");
	UNIFORM(alphaFunction, "alphaFunction");
	UNIFORM(alphaReference, "alphaReference");
#undef UNIFORM
	glUniform1i(glGetUniformLocation(program, "image0"), 0);
	glUniform1i(glGetUniformLocation(program, "image1"), 1);
	glGenVertexArrays(1, &vao);
	glBindVertexArray(vao);
	for (int i = 0; i < ATTRIBUTE_COUNT; i++)
		glGenBuffers(1, &attributes[i].name);
	glGenBuffers(1, &elementBuffer.name);
	glGenBuffers(1, &immediateBuffer.name);
	glGenBuffers(1, &quadBuffer.name);
	// Both samplers must be complete even when a stage is disabled.
	const unsigned char white[4] = {255, 255, 255, 255};
	glGenTextures(1, &whiteTexture);
	for (int i = 0; i < 2; i++)
	{
		glActiveTexture(GL_TEXTURE0 + i);
		glBindTexture(GL_TEXTURE_2D, whiteTexture);
	}
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glActiveTexture(GL_TEXTURE0);
	dirty = true;
}
void WebGL_Shutdown(void)
{
	for (int i = 0; i < ATTRIBUTE_COUNT; i++)
		glDeleteBuffers(1, &attributes[i].name);
	glDeleteBuffers(1, &elementBuffer.name);
	glDeleteBuffers(1, &immediateBuffer.name);
	glDeleteBuffers(1, &quadBuffer.name);
	glDeleteVertexArrays(1, &vao);
	glDeleteProgram(program);
	glDeleteTextures(1, &whiteTexture);
	free(vertices);
	free(indices);
	free(convertedPixels);
	vertices = NULL;
	indices = NULL;
	convertedPixels = NULL;
}
static void Prepare(void)
{
	if (!dirty)
		return;
	dirty = false;
	const GLfloat *m = model.value[model.top];
	GLfloat n[9] = {m[5] * m[10] - m[6] * m[9], m[6] * m[8] - m[4] * m[10], m[4] * m[9] - m[5] * m[8],
					m[9] * m[2] - m[10] * m[1], m[10] * m[0] - m[8] * m[2], m[8] * m[1] - m[9] * m[0],
					m[1] * m[6] - m[2] * m[5],	m[2] * m[4] - m[0] * m[6],	m[0] * m[5] - m[1] * m[4]};
	GLfloat determinant = m[0] * n[0] + m[1] * n[1] + m[2] * n[2];
	if (fabsf(determinant) > 1e-20f)
		for (int i = 0; i < 9; i++)
			n[i] /= determinant;
	glUniformMatrix4fv(u.model, 1, GL_FALSE, m);
	glUniformMatrix4fv(u.projection, 1, GL_FALSE, projection.value[projection.top]);
	glUniformMatrix3fv(u.normal, 1, GL_FALSE, n);
	for (int i = 0; i < 2; i++)
		glUniformMatrix4fv(u.texture[i], 1, GL_FALSE, textureMatrix[i].value[textureMatrix[i].top]);
	glUniform1i(u.lighting, lighting);
	glUniform1i(u.normalize, normalizeNormals || rescaleNormals);
	glUniform1i(u.colorMaterial, colorMaterial);
	glUniform4fv(u.material, 1, material);
	glUniform1i(u.twoSided, twoSided);
	glUniform4fv(u.ambient, 1, ambient);
	GLint enabled[LIGHT_COUNT];
	for (int i = 0; i < LIGHT_COUNT; i++)
		enabled[i] = lights[i];
	glUniform1iv(u.lights, LIGHT_COUNT, enabled);
	glUniform4fv(u.lightPosition, LIGHT_COUNT, &lightPosition[0][0]);
	glUniform4fv(u.lightDiffuse, LIGHT_COUNT, &lightDiffuse[0][0]);
	glUniform4fv(u.lightAmbient, LIGHT_COUNT, &lightAmbient[0][0]);
	glUniform2i(u.textured, textured[0], textured[1]);
	glUniform4i(u.texgen, texgen[0][0], texgen[0][1], texgen[1][0], texgen[1][1]);
	glUniform4i(u.combine, envMode[0] == GL_COMBINE && combineRGB[0] == GL_ADD,
				envMode[0] == GL_COMBINE && combineAlpha[0] == GL_ADD,
				envMode[1] == GL_COMBINE && combineRGB[1] == GL_ADD,
				envMode[1] == GL_COMBINE && combineAlpha[1] == GL_ADD);
	glUniform1i(u.fog, fog);
	glUniform4fv(u.fogColor, 1, fogColor);
	glUniform2f(u.fogRange, fogStart, fogEnd);
	glUniform1f(u.fogDensity, fogDensity);
	glUniform1i(u.fogMode, fogMode);
	glUniform1i(u.alphaTest, alphaTest);
	glUniform1i(u.alphaFunction, alphaFunction);
	glUniform1f(u.alphaReference, alphaReference);
}
static GLboolean *Capability(GLenum cap)
{
	if (cap >= GL_LIGHT0 && cap < GL_LIGHT0 + LIGHT_COUNT)
		return &lights[cap - GL_LIGHT0];
	switch (cap)
	{
	case GL_LIGHTING:
		return &lighting;
	case GL_FOG:
		return &fog;
	case GL_NORMALIZE:
		return &normalizeNormals;
	case GL_RESCALE_NORMAL:
		return &rescaleNormals;
	case GL_ALPHA_TEST:
		return &alphaTest;
	case GL_COLOR_MATERIAL:
		return &colorMaterial;
	case GL_TEXTURE_2D:
		return &textured[activeTexture];
	case GL_TEXTURE_GEN_S:
		return &texgen[activeTexture][0];
	case GL_TEXTURE_GEN_T:
		return &texgen[activeTexture][1];
	default:
		return NULL;
	}
}
void WebGL_Enable(GLenum cap)
{
	GLboolean *p = Capability(cap);
	if (p)
	{
		if (!*p)
		{
			*p = GL_TRUE;
			dirty = true;
		}
	}
	else
		glEnable(cap);
}
void WebGL_Disable(GLenum cap)
{
	GLboolean *p = Capability(cap);
	if (p)
	{
		if (*p)
		{
			*p = GL_FALSE;
			dirty = true;
		}
	}
	else
		glDisable(cap);
}
GLboolean WebGL_IsEnabled(GLenum cap)
{
	GLboolean *p = Capability(cap);
	return p ? *p : glIsEnabled(cap);
}
void WebGL_MatrixMode(GLenum mode)
{
	GAME_ASSERT(mode == GL_MODELVIEW || mode == GL_PROJECTION || mode == GL_TEXTURE);
	matrixMode = mode;
}
void WebGL_LoadIdentity(void)
{
	MatrixStack *s = CurrentStack();
	Identity(s->value[s->top]);
	dirty = true;
}
void WebGL_LoadMatrixf(const GLfloat *m)
{
	MatrixStack *s = CurrentStack();
	memcpy(s->value[s->top], m, 16 * sizeof(*m));
	dirty = true;
}
void WebGL_MultMatrixf(const GLfloat *b)
{
	MatrixStack *s = CurrentStack();
	const GLfloat *a = s->value[s->top];
	GLfloat result[16];
	for (int col = 0; col < 4; col++)
		for (int row = 0; row < 4; row++)
		{
			result[col * 4 + row] = a[row] * b[col * 4] + a[4 + row] * b[col * 4 + 1] + a[8 + row] * b[col * 4 + 2] +
									a[12 + row] * b[col * 4 + 3];
		}
	memcpy(s->value[s->top], result, sizeof(result));
	dirty = true;
}
void WebGL_PushMatrix(void)
{
	MatrixStack *s = CurrentStack();
	GAME_ASSERT(s->top + 1 < MATRIX_DEPTH);
	memcpy(s->value[s->top + 1], s->value[s->top], sizeof(s->value[0]));
	s->top++;
}
void WebGL_PopMatrix(void)
{
	MatrixStack *s = CurrentStack();
	GAME_ASSERT(s->top > 0);
	s->top--;
	dirty = true;
}
void WebGL_Translatef(GLfloat x, GLfloat y, GLfloat z)
{
	GLfloat m[16];
	Identity(m);
	m[12] = x;
	m[13] = y;
	m[14] = z;
	WebGL_MultMatrixf(m);
}
void WebGL_Scalef(GLfloat x, GLfloat y, GLfloat z)
{
	GLfloat m[16];
	Identity(m);
	m[0] = x;
	m[5] = y;
	m[10] = z;
	WebGL_MultMatrixf(m);
}
void WebGL_Rotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
	float length = sqrtf(x * x + y * y + z * z);
	if (length == 0)
		return;
	x /= length;
	y /= length;
	z /= length;
	float c = cosf(angle * PI / 180), s = sinf(angle * PI / 180), t = 1 - c;
	GLfloat m[16] = {t * x * x + c,
					 t * x * y + s * z,
					 t * x * z - s * y,
					 0,
					 t * x * y - s * z,
					 t * y * y + c,
					 t * y * z + s * x,
					 0,
					 t * x * z + s * y,
					 t * y * z - s * x,
					 t * z * z + c,
					 0,
					 0,
					 0,
					 0,
					 1};
	WebGL_MultMatrixf(m);
}
void WebGL_Ortho(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f)
{
	GLfloat m[16] = {0};
	m[0] = 2 / (r - l);
	m[5] = 2 / (t - b);
	m[10] = -2 / (f - n);
	m[12] = -(r + l) / (r - l);
	m[13] = -(t + b) / (t - b);
	m[14] = -(f + n) / (f - n);
	m[15] = 1;
	WebGL_MultMatrixf(m);
}
void WebGL_Frustum(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f)
{
	GLfloat m[16] = {0};
	m[0] = 2 * n / (r - l);
	m[5] = 2 * n / (t - b);
	m[8] = (r + l) / (r - l);
	m[9] = (t + b) / (t - b);
	m[10] = -(f + n) / (f - n);
	m[11] = -1;
	m[14] = -2 * f * n / (f - n);
	WebGL_MultMatrixf(m);
}
static const GLfloat *MatrixForQuery(GLenum name)
{
	if (name == GL_MODELVIEW_MATRIX)
		return model.value[model.top];
	if (name == GL_PROJECTION_MATRIX)
		return projection.value[projection.top];
	if (name == GL_TEXTURE_MATRIX)
		return textureMatrix[activeTexture].value[textureMatrix[activeTexture].top];
	return NULL;
}
void WebGL_GetFloatv(GLenum name, GLfloat *value)
{
	const GLfloat *m = MatrixForQuery(name);
	if (m)
		memcpy(value, m, 16 * sizeof(*value));
	else if (name == GL_CURRENT_COLOR)
		memcpy(value, color, sizeof(color));
	else
		glGetFloatv(name, value);
}
void WebGL_GetDoublev(GLenum name, GLdouble *value)
{
	const GLfloat *m = MatrixForQuery(name);
	GAME_ASSERT(m);
	for (int i = 0; i < 16; i++)
		value[i] = m[i];
}
void WebGL_GetIntegerv(GLenum name, GLint *value)
{
	if (name == GL_MATRIX_MODE)
		*value = matrixMode;
	else
		glGetIntegerv(name, value);
}
void WebGL_DepthMask(GLboolean enabled)
{
	enabled = enabled ? GL_TRUE : GL_FALSE;
	if (depthWriteMask != enabled)
	{
		glDepthMask(enabled);
		depthWriteMask = enabled;
	}
}
void WebGL_GetBooleanv(GLenum name, GLboolean *value)
{
	if (name == GL_DEPTH_WRITEMASK)
		*value = depthWriteMask;
	else
		glGetBooleanv(name, value);
}
void WebGL_Color4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
	color[0] = r;
	color[1] = g;
	color[2] = b;
	color[3] = a;
}
void WebGL_Color4fv(const GLfloat *v)
{
	memcpy(color, v, sizeof(color));
}
void WebGL_TexCoord2f(GLfloat s, GLfloat t)
{
	uv[0] = s;
	uv[1] = t;
}
void WebGL_TexCoord2fv(const GLfloat *v)
{
	memcpy(uv, v, sizeof(uv));
}
void WebGL_ActiveTexture(GLenum texture)
{
	GAME_ASSERT(texture >= GL_TEXTURE0 && texture < GL_TEXTURE0 + 2);
	activeTexture = texture - GL_TEXTURE0;
	glActiveTexture(texture);
}
void WebGL_ClientActiveTexture(GLenum texture)
{
	GAME_ASSERT(texture >= GL_TEXTURE0 && texture < GL_TEXTURE0 + 2);
	clientTexture = texture - GL_TEXTURE0;
}
static unsigned ArrayIndex(GLenum array)
{
	switch (array)
	{
	case GL_VERTEX_ARRAY:
		return 0;
	case GL_NORMAL_ARRAY:
		return 1;
	case GL_COLOR_ARRAY:
		return 2;
	case GL_TEXTURE_COORD_ARRAY:
		return 3 + clientTexture;
	default:
		DoFatalAlert("Unsupported vertex array: %x", array);
		return 0;
	}
}
void WebGL_EnableClientState(GLenum array)
{
	arrays[ArrayIndex(array)].enabled = GL_TRUE;
}
void WebGL_DisableClientState(GLenum array)
{
	arrays[ArrayIndex(array)].enabled = GL_FALSE;
}
static void SetArray(unsigned index, GLint size, GLenum type, GLsizei stride, const void *pointer)
{
	GAME_ASSERT(type == GL_FLOAT && stride == 0);
	arrays[index].size = size;
	arrays[index].pointer = pointer;
}
void WebGL_VertexPointer(GLint size, GLenum type, GLsizei stride, const void *p)
{
	SetArray(0, size, type, stride, p);
}
void WebGL_NormalPointer(GLenum type, GLsizei stride, const void *p)
{
	SetArray(1, 3, type, stride, p);
}
void WebGL_ColorPointer(GLint size, GLenum type, GLsizei stride, const void *p)
{
	SetArray(2, size, type, stride, p);
}
void WebGL_TexCoordPointer(GLint size, GLenum type, GLsizei stride, const void *p)
{
	SetArray(3 + clientTexture, size, type, stride, p);
}
static void Upload(Buffer *buffer, GLenum target, const void *data, size_t bytes)
{
	glBindBuffer(target, buffer->name);
	if (bytes > buffer->capacity)
	{
		size_t capacity = buffer->capacity ? buffer->capacity : 4096;
		while (capacity < bytes)
			capacity *= 2;
		glBufferData(target, capacity, NULL, GL_DYNAMIC_DRAW);
		buffer->capacity = capacity;
	}
	glBufferSubData(target, 0, bytes, data);
}
static void ReserveIndices(size_t count)
{
	if (count <= indexCapacity)
		return;
	size_t capacity = indexCapacity ? indexCapacity : 1024;
	while (capacity < count)
		capacity *= 2;
	void *p = realloc(indices, capacity * sizeof(*indices));
	GAME_ASSERT(p);
	indices = p;
	indexCapacity = capacity;
}
void WebGL_DrawTriangles(int count, int elements, const void *data)
{
	if (elements == 0)
		return;
	GAME_ASSERT(count > 0 && elements > 0 && elements % 3 == 0 && data);
	Prepare();
	for (unsigned i = 0; i < ATTRIBUTE_COUNT; i++)
	{
		ClientArray *a = &arrays[i];
		bool needed =
			i == 0 || (i == 1 && (lighting || texgen[0][0] || texgen[1][0])) || i == 2 || (i >= 3 && textured[i - 3]);
		if (a->enabled && needed)
		{
			GAME_ASSERT(a->pointer);
			Upload(&attributes[i], GL_ARRAY_BUFFER, a->pointer, (size_t)count * a->size * sizeof(GLfloat));
			glEnableVertexAttribArray(i);
			glVertexAttribPointer(i, a->size, GL_FLOAT, GL_FALSE, 0, 0);
		}
		else
		{
			glDisableVertexAttribArray(i);
			if (i == 1)
				glVertexAttrib3f(i, 0, 0, 1);
			else if (i == 2)
				glVertexAttrib4fv(i, color);
			else if (i >= 3)
				glVertexAttrib2fv(i, uv);
		}
	}
	GLenum mode = GL_TRIANGLES;
	if (wireframe)
	{
		const uint32_t *source = data;
		ReserveIndices((size_t)elements * 2);
		for (int i = 0; i < elements; i += 3)
		{
			size_t k = (size_t)i * 2;
			indices[k] = source[i];
			indices[k + 1] = source[i + 1];
			indices[k + 2] = source[i + 1];
			indices[k + 3] = source[i + 2];
			indices[k + 4] = source[i + 2];
			indices[k + 5] = source[i];
		}
		data = indices;
		elements *= 2;
		mode = GL_LINES;
	}
	Upload(&elementBuffer, GL_ELEMENT_ARRAY_BUFFER, data, (size_t)elements * sizeof(uint32_t));
	glDrawElements(mode, elements, GL_UNSIGNED_INT, 0);
}
void WebGL_Begin(GLenum mode)
{
	GAME_ASSERT(primitive == 0);
	primitive = mode + 1;
	vertexCount = 0;
}
void WebGL_Vertex3f(GLfloat x, GLfloat y, GLfloat z)
{
	GAME_ASSERT(primitive);
	if (vertexCount == vertexCapacity)
	{
		size_t capacity = vertexCapacity ? vertexCapacity * 2 : 1024;
		void *p = realloc(vertices, capacity * sizeof(*vertices));
		GAME_ASSERT(p);
		vertices = p;
		vertexCapacity = capacity;
	}
	Vertex *v = &vertices[vertexCount++];
	v->position[0] = x;
	v->position[1] = y;
	v->position[2] = z;
	memcpy(v->color, color, sizeof(color));
	memcpy(v->uv, uv, sizeof(uv));
}
void WebGL_Vertex3fv(const GLfloat *v)
{
	WebGL_Vertex3f(v[0], v[1], v[2]);
}
void WebGL_Vertex2f(GLfloat x, GLfloat y)
{
	WebGL_Vertex3f(x, y, 0);
}
void WebGL_End(void)
{
	GAME_ASSERT(primitive);
	GLenum mode = primitive - 1;
	primitive = 0;
	if (!vertexCount)
		return;
	Prepare();
	Upload(&immediateBuffer, GL_ARRAY_BUFFER, vertices, vertexCount * sizeof(*vertices));
	static const GLint sizes[5] = {3, 3, 4, 2, 2};
	static const size_t offsets[5] = {offsetof(Vertex, position), 0, offsetof(Vertex, color), offsetof(Vertex, uv),
									  offsetof(Vertex, uv)};
	glDisableVertexAttribArray(1);
	glVertexAttrib3f(1, 0, 0, 1);
	for (unsigned i = 0; i < ATTRIBUTE_COUNT; i++)
	{
		if (i == 1)
			continue;
		glEnableVertexAttribArray(i);
		glVertexAttribPointer(i, sizes[i], GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsets[i]);
	}
	if (mode == GL_QUADS)
	{
		GAME_ASSERT(vertexCount % 4 == 0);
		size_t quads = vertexCount / 4;
		if (quads > quadCount)
		{
			ReserveIndices(quads * 6);
			for (size_t i = 0; i < quads; i++)
			{
				uint32_t base = i * 4;
				indices[i * 6] = base;
				indices[i * 6 + 1] = base + 1;
				indices[i * 6 + 2] = base + 2;
				indices[i * 6 + 3] = base;
				indices[i * 6 + 4] = base + 2;
				indices[i * 6 + 5] = base + 3;
			}
			Upload(&quadBuffer, GL_ELEMENT_ARRAY_BUFFER, indices, quads * 6 * sizeof(*indices));
			quadCount = quads;
		}
		else
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, quadBuffer.name);
		glDrawElements(GL_TRIANGLES, quads * 6, GL_UNSIGNED_INT, 0);
	}
	else
		glDrawArrays(mode, 0, vertexCount);
}
void WebGL_AlphaFunc(GLenum function, GLfloat reference)
{
	alphaFunction = function;
	alphaReference = fmaxf(0, fminf(1, reference));
	dirty = true;
}
void WebGL_Lightfv(GLenum light, GLenum name, const GLfloat *v)
{
	GAME_ASSERT(light >= GL_LIGHT0 && light < GL_LIGHT0 + LIGHT_COUNT);
	unsigned i = light - GL_LIGHT0;
	if (name == GL_POSITION)
	{
		const GLfloat *m = model.value[model.top];
		for (int r = 0; r < 4; r++)
			lightPosition[i][r] = m[r] * v[0] + m[4 + r] * v[1] + m[8 + r] * v[2] + m[12 + r] * v[3];
	}
	else if (name == GL_DIFFUSE)
		memcpy(lightDiffuse[i], v, 4 * sizeof(*v));
	else if (name == GL_AMBIENT)
		memcpy(lightAmbient[i], v, 4 * sizeof(*v));
	else
		DoFatalAlert("Unsupported light parameter: %x", name);
	dirty = true;
}
void WebGL_LightModelfv(GLenum name, const GLfloat *value)
{
	GAME_ASSERT(name == GL_LIGHT_MODEL_AMBIENT);
	memcpy(ambient, value, sizeof(ambient));
	dirty = true;
}
void WebGL_LightModeli(GLenum name, GLint value)
{
	GAME_ASSERT(name == GL_LIGHT_MODEL_TWO_SIDE);
	twoSided = value != 0;
	dirty = true;
}
void WebGL_Materialfv(GLenum face, GLenum name, const GLfloat *value)
{
	GAME_ASSERT(face == GL_FRONT_AND_BACK && name == GL_AMBIENT_AND_DIFFUSE);
	memcpy(material, value, sizeof(material));
	dirty = true;
}
void WebGL_ColorMaterial(GLenum face, GLenum mode)
{
	GAME_ASSERT(face == GL_FRONT_AND_BACK && mode == GL_AMBIENT_AND_DIFFUSE);
}
void WebGL_Fogf(GLenum name, GLfloat value)
{
	switch (name)
	{
	case GL_FOG_START:
		fogStart = value;
		break;
	case GL_FOG_END:
		fogEnd = value;
		break;
	case GL_FOG_DENSITY:
		fogDensity = value;
		break;
	default:
		DoFatalAlert("Unsupported fog parameter: %x", name);
	}
	dirty = true;
}
void WebGL_Fogi(GLenum name, GLint value)
{
	GAME_ASSERT(name == GL_FOG_MODE);
	fogMode = value;
	dirty = true;
}
void WebGL_Fogfv(GLenum name, const GLfloat *value)
{
	GAME_ASSERT(name == GL_FOG_COLOR);
	memcpy(fogColor, value, sizeof(fogColor));
	dirty = true;
}
void WebGL_TexEnvi(GLenum target, GLenum name, GLint value)
{
	GAME_ASSERT(target == GL_TEXTURE_ENV);
	switch (name)
	{
	case GL_TEXTURE_ENV_MODE:
		GAME_ASSERT(value == GL_MODULATE || value == GL_COMBINE);
		envMode[activeTexture] = value;
		break;
	case GL_COMBINE_RGB:
		GAME_ASSERT(value == GL_MODULATE || value == GL_ADD);
		combineRGB[activeTexture] = value;
		break;
	case GL_COMBINE_ALPHA:
		GAME_ASSERT(value == GL_MODULATE || value == GL_ADD);
		combineAlpha[activeTexture] = value;
		break;
	default:
		DoFatalAlert("Unsupported texture combiner: %x", name);
	}
	dirty = true;
}
void WebGL_TexGeni(GLenum coord, GLenum name, GLint value)
{
	GAME_ASSERT((coord == GL_S || coord == GL_T) && name == GL_TEXTURE_GEN_MODE && value == GL_SPHERE_MAP);
}
void WebGL_Hint(GLenum target, GLenum mode)
{
	if (target != GL_FOG_HINT)
		glHint(target, mode); /* Fog quality is fixed by our shader. */
}
void WebGL_DrawBuffer(GLenum buffer)
{
	GAME_ASSERT_MESSAGE(buffer == GL_BACK, "Quad-buffer stereo is unavailable in WebGL");
	glDrawBuffers(1, &buffer);
}
void WebGL_PolygonMode(GLenum face, GLenum mode)
{
	GAME_ASSERT(face == GL_FRONT_AND_BACK && (mode == GL_FILL || mode == GL_LINE));
	wireframe = mode == GL_LINE;
}

static const void *ConvertPixels(GLsizei width, GLsizei height, GLenum format, GLenum type, const void *pixels,
								 bool opaque)
{
	size_t bytes = (size_t)width * height * 4;
	if (bytes > pixelCapacity)
	{
		void *p = realloc(convertedPixels, bytes);
		GAME_ASSERT(p);
		convertedPixels = p;
		pixelCapacity = bytes;
	}
	GAME_ASSERT(format == GL_BGRA || format == GL_RGBA);
	GLint alignment;
	glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
	size_t pixelSize = type == GL_UNSIGNED_SHORT_1_5_5_5_REV ? 2 : 4;
	size_t stride = ((size_t)width * pixelSize + alignment - 1) & ~((size_t)alignment - 1);
	for (int y = 0; y < height; y++)
		for (int x = 0; x < width; x++)
		{
			const unsigned char *source = (const unsigned char *)pixels + y * stride + x * pixelSize;
			unsigned char *out = convertedPixels + ((size_t)y * width + x) * 4;
			if (type == GL_UNSIGNED_BYTE || type == GL_UNSIGNED_INT_8_8_8_8_REV)
			{
				out[0] = source[format == GL_BGRA ? 2 : 0];
				out[1] = source[1];
				out[2] = source[format == GL_BGRA ? 0 : 2];
				out[3] = source[3];
			}
			else if (type == GL_UNSIGNED_SHORT_1_5_5_5_REV)
			{
				uint16_t value;
				memcpy(&value, source, 2);
				unsigned a = value & 31, b = (value >> 5) & 31, c = (value >> 10) & 31;
				out[0] = (format == GL_BGRA ? c : a) * 255 / 31;
				out[1] = b * 255 / 31;
				out[2] = (format == GL_BGRA ? a : c) * 255 / 31;
				out[3] = (value & 0x8000) ? 255 : 0;
			}
			else
			{
				DoFatalAlert("Unsupported texture pixel type: %x", type);
			}
			if (opaque)
				out[3] = 255;
		}
	return convertedPixels;
}
void WebGL_TexImage2D(GLenum target, GLint level, GLint internalFormat, GLsizei width, GLsizei height, GLint border,
					  GLenum format, GLenum type, const void *pixels)
{
	if (format == GL_BGRA || type == GL_UNSIGNED_INT_8_8_8_8_REV || type == GL_UNSIGNED_SHORT_1_5_5_5_REV)
	{
		if (pixels)
			pixels = ConvertPixels(width, height, format, type, pixels,
								   internalFormat == GL_RGB || internalFormat == GL_RGB5);
		format = GL_RGBA;
		type = GL_UNSIGNED_BYTE;
		internalFormat = GL_RGBA;
	}
	else if (internalFormat == GL_RGB5_A1 || internalFormat == GL_RGBA8 || internalFormat == GL_RGB8)
		internalFormat = format;
	glTexImage2D(target, level, internalFormat, width, height, border, format, type, pixels);
}
void WebGL_TexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei width, GLsizei height, GLenum format,
						 GLenum type, const void *pixels)
{
	if (format == GL_BGRA || type == GL_UNSIGNED_INT_8_8_8_8_REV || type == GL_UNSIGNED_SHORT_1_5_5_5_REV)
	{
		pixels = ConvertPixels(width, height, format, type, pixels, false);
		format = GL_RGBA;
		type = GL_UNSIGNED_BYTE;
	}
	glTexSubImage2D(target, level, x, y, width, height, format, type, pixels);
}
#endif
