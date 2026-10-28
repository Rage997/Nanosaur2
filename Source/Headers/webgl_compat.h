#pragma once

// The game's fixed-function subset, implemented with WebGL2 shaders and buffers.
// Desktop GL headers supply enums/types only; native builds never include this file.
void WebGL_Init(void);
void WebGL_Shutdown(void);
void WebGL_DrawTriangles(int vertexCount, int indexCount, const void *indices);
void WebGL_Enable(GLenum cap);
void WebGL_Disable(GLenum cap);
GLboolean WebGL_IsEnabled(GLenum cap);
void WebGL_MatrixMode(GLenum mode);
void WebGL_LoadIdentity(void);
void WebGL_LoadMatrixf(const GLfloat *matrix);
void WebGL_MultMatrixf(const GLfloat *matrix);
void WebGL_PushMatrix(void);
void WebGL_PopMatrix(void);
void WebGL_Translatef(GLfloat x, GLfloat y, GLfloat z);
void WebGL_Scalef(GLfloat x, GLfloat y, GLfloat z);
void WebGL_Rotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
void WebGL_Ortho(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f);
void WebGL_Frustum(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f);
void WebGL_GetFloatv(GLenum name, GLfloat *value);
void WebGL_GetDoublev(GLenum name, GLdouble *value);
void WebGL_GetIntegerv(GLenum name, GLint *value);
void WebGL_DepthMask(GLboolean enabled);
void WebGL_GetBooleanv(GLenum name, GLboolean *value);
void WebGL_Color4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
void WebGL_Color4fv(const GLfloat *value);
void WebGL_TexCoord2f(GLfloat s, GLfloat t);
void WebGL_TexCoord2fv(const GLfloat *value);
void WebGL_Begin(GLenum mode);
void WebGL_End(void);
void WebGL_Vertex3f(GLfloat x, GLfloat y, GLfloat z);
void WebGL_Vertex3fv(const GLfloat *value);
void WebGL_Vertex2f(GLfloat x, GLfloat y);
void WebGL_ActiveTexture(GLenum texture);
void WebGL_ClientActiveTexture(GLenum texture);
void WebGL_EnableClientState(GLenum array);
void WebGL_DisableClientState(GLenum array);
void WebGL_VertexPointer(GLint size, GLenum type, GLsizei stride, const void *pointer);
void WebGL_NormalPointer(GLenum type, GLsizei stride, const void *pointer);
void WebGL_ColorPointer(GLint size, GLenum type, GLsizei stride, const void *pointer);
void WebGL_TexCoordPointer(GLint size, GLenum type, GLsizei stride, const void *pointer);
void WebGL_AlphaFunc(GLenum function, GLfloat reference);
void WebGL_Lightfv(GLenum light, GLenum name, const GLfloat *value);
void WebGL_LightModelfv(GLenum name, const GLfloat *value);
void WebGL_LightModeli(GLenum name, GLint value);
void WebGL_Materialfv(GLenum face, GLenum name, const GLfloat *value);
void WebGL_ColorMaterial(GLenum face, GLenum mode);
void WebGL_Fogf(GLenum name, GLfloat value);
void WebGL_Fogi(GLenum name, GLint value);
void WebGL_Fogfv(GLenum name, const GLfloat *value);
void WebGL_TexEnvi(GLenum target, GLenum name, GLint value);
void WebGL_TexGeni(GLenum coord, GLenum name, GLint value);
void WebGL_Hint(GLenum target, GLenum mode);
void WebGL_DrawBuffer(GLenum buffer);
void WebGL_PolygonMode(GLenum face, GLenum mode);
void WebGL_TexImage2D(GLenum target, GLint level, GLint internalFormat, GLsizei width, GLsizei height, GLint border,
					  GLenum format, GLenum type, const void *pixels);
void WebGL_TexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei width, GLsizei height, GLenum format,
						 GLenum type, const void *pixels);

#ifndef WEBGL_COMPAT_IMPLEMENTATION
#define glEnable WebGL_Enable
#define glDisable WebGL_Disable
#define glIsEnabled WebGL_IsEnabled
#define glMatrixMode WebGL_MatrixMode
#define glLoadIdentity WebGL_LoadIdentity
#define glLoadMatrixf WebGL_LoadMatrixf
#define glMultMatrixf WebGL_MultMatrixf
#define glPushMatrix WebGL_PushMatrix
#define glPopMatrix WebGL_PopMatrix
#define glTranslatef WebGL_Translatef
#define glScalef WebGL_Scalef
#define glRotatef WebGL_Rotatef
#define glOrtho WebGL_Ortho
#define glFrustum WebGL_Frustum
#define glGetFloatv WebGL_GetFloatv
#define glGetDoublev WebGL_GetDoublev
#define glGetIntegerv WebGL_GetIntegerv
#define glDepthMask WebGL_DepthMask
#define glGetBooleanv WebGL_GetBooleanv
#define glColor4f WebGL_Color4f
#define glColor4fv WebGL_Color4fv
#define glTexCoord2f WebGL_TexCoord2f
#define glTexCoord2fv WebGL_TexCoord2fv
#define glBegin WebGL_Begin
#define glEnd WebGL_End
#define glVertex3f WebGL_Vertex3f
#define glVertex3fv WebGL_Vertex3fv
#define glVertex2f WebGL_Vertex2f
#define glActiveTexture WebGL_ActiveTexture
#define glClientActiveTexture WebGL_ClientActiveTexture
#define glEnableClientState WebGL_EnableClientState
#define glDisableClientState WebGL_DisableClientState
#define glVertexPointer WebGL_VertexPointer
#define glNormalPointer WebGL_NormalPointer
#define glColorPointer WebGL_ColorPointer
#define glTexCoordPointer WebGL_TexCoordPointer
#define glAlphaFunc WebGL_AlphaFunc
#define glLightfv WebGL_Lightfv
#define glLightModelfv WebGL_LightModelfv
#define glLightModeli WebGL_LightModeli
#define glMaterialfv WebGL_Materialfv
#define glColorMaterial WebGL_ColorMaterial
#define glFogf WebGL_Fogf
#define glFogi WebGL_Fogi
#define glFogfv WebGL_Fogfv
#define glTexEnvi WebGL_TexEnvi
#define glTexGeni WebGL_TexGeni
#define glHint WebGL_Hint
#define glDrawBuffer WebGL_DrawBuffer
#define glPolygonMode WebGL_PolygonMode
#define glTexImage2D WebGL_TexImage2D
#define glTexSubImage2D WebGL_TexSubImage2D
#endif
