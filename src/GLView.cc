#include "GLView.h"

#include "printutils.h"
#include "stdio.h"

#ifdef _WIN32
#include <GL/wglew.h>
#elif !defined(__APPLE__)
#include <GL/glxew.h>
#endif

GLView::GLView()
{
	viewer_distance = 500;
	object_trans << 0, 0, 0;
  camera_eye << 0, 0, 0;
  camera_center << 0, 0, 0;
  showedges = false;
  showfaces = true;
  orthomode = false;
  showaxes = false;
  showcrosshairs = false;
	renderer = NULL;
#ifdef ENABLE_OPENCSG
  is_opencsg_capable = false;
  has_shaders = false;
  opencsg_support = true;
  static int sId = 0;
  this->opencsg_id = sId++;
  for (int i = 0; i < 10; i++) this->shaderinfo[i] = 0;
#endif
}

void GLView::setRenderer(Renderer* r)
{
	renderer = r;
}

void GLView::resizeGL(int w, int h)
{
#ifdef ENABLE_OPENCSG
  shaderinfo[9] = w;
  shaderinfo[10] = h;
#endif
  this->width = w;
  this->height = h;
  glViewport(0, 0, w, h);
  w_h_ratio = sqrt((double)w / (double)h);
}

void GLView::setGimbalCamera(const Eigen::Vector3d &pos, const Eigen::Vector3d &rot, double distance)
{
	PRINT("set gimbal camera not implemented");
}

void GLView::setupGimbalPerspective()
{
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glFrustum(-w_h_ratio, +w_h_ratio, -(1/w_h_ratio), +(1/w_h_ratio), +10.0, +FAR_FAR_AWAY);
  gluLookAt(0.0, -viewer_distance, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0);
}

void GLView::setupGimbalOrtho(double distance, bool offset)
{
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  if(offset)
    glTranslated(-0.8, -0.8, 0);
  double l = distance/10;
  glOrtho(-w_h_ratio*l, +w_h_ratio*l,
      -(1/w_h_ratio)*l, +(1/w_h_ratio)*l,
      -FAR_FAR_AWAY, +FAR_FAR_AWAY);
  gluLookAt(0.0, -viewer_distance, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0);
}

void GLView::setCamera(const Eigen::Vector3d &pos, const Eigen::Vector3d &center)
{
  this->camera_eye = pos;
  this->camera_center = center;
}

void GLView::setupPerspective()
{
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  double dist = (this->camera_center - this->camera_eye).norm();
  gluPerspective(45, w_h_ratio, 0.1*dist, 100*dist);
}

void GLView::setupOrtho(bool offset)
{
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  if (offset) glTranslated(-0.8, -0.8, 0);
  double l = (this->camera_center - this->camera_eye).norm() / 10;
  glOrtho(-w_h_ratio*l, +w_h_ratio*l,
          -(1/w_h_ratio)*l, +(1/w_h_ratio)*l,
          -FAR_FAR_AWAY, +FAR_FAR_AWAY);
}

#ifdef ENABLE_OPENCSG
void GLView::enable_opencsg_shaders()
{
  const char *openscad_disable_gl20_env = getenv("OPENSCAD_DISABLE_GL20");
  if (openscad_disable_gl20_env && !strcmp(openscad_disable_gl20_env, "0")) {
    openscad_disable_gl20_env = NULL;
  }

  // All OpenGL 2 contexts are OpenCSG capable
  if (GLEW_VERSION_2_0) {
    if (!openscad_disable_gl20_env) {
      this->is_opencsg_capable = true;
      this->has_shaders = true;
    }
  }

  // If OpenGL < 2, check for extensions
  else {
    if (GLEW_ARB_framebuffer_object) this->is_opencsg_capable = true;
    else if (GLEW_EXT_framebuffer_object && GLEW_EXT_packed_depth_stencil) {
      this->is_opencsg_capable = true;
    }
#ifdef WIN32
    else if (WGLEW_ARB_pbuffer && WGLEW_ARB_pixel_format) this->is_opencsg_capable = true;
#elif !defined(__APPLE__)
    else if (GLXEW_SGIX_pbuffer && GLXEW_SGIX_fbconfig) this->is_opencsg_capable = true;
#endif
  }

  if (!GLEW_VERSION_2_0 || !this->is_opencsg_capable) {
    display_opencsg_warning();
  }

  if (opencsg_support && this->has_shaders) {
  /*
    Uniforms:
      1 color1 - face color
      2 color2 - edge color
      7 xscale
      8 yscale

    Attributes:
      3 trig
      4 pos_b
      5 pos_c
      6 mask

    Other:
      9 width
      10 height

    Outputs:
      tp
      tr
      shading
   */
    const char *vs_source =
      "uniform float xscale, yscale;\n"
      "attribute vec3 pos_b, pos_c;\n"
      "attribute vec3 trig, mask;\n"
      "varying vec3 tp, tr;\n"
      "varying float shading;\n"
      "void main() {\n"
      "  vec4 p0 = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
      "  vec4 p1 = gl_ModelViewProjectionMatrix * vec4(pos_b, 1.0);\n"
      "  vec4 p2 = gl_ModelViewProjectionMatrix * vec4(pos_c, 1.0);\n"
      "  float a = distance(vec2(xscale*p1.x/p1.w, yscale*p1.y/p1.w), vec2(xscale*p2.x/p2.w, yscale*p2.y/p2.w));\n"
      "  float b = distance(vec2(xscale*p0.x/p0.w, yscale*p0.y/p0.w), vec2(xscale*p1.x/p1.w, yscale*p1.y/p1.w));\n"
      "  float c = distance(vec2(xscale*p0.x/p0.w, yscale*p0.y/p0.w), vec2(xscale*p2.x/p2.w, yscale*p2.y/p2.w));\n"
      "  float s = (a + b + c) / 2.0;\n"
      "  float A = sqrt(s*(s-a)*(s-b)*(s-c));\n"
      "  float ha = 2.0*A/a;\n"
      "  gl_Position = p0;\n"
      "  tp = mask * ha;\n"
      "  tr = trig;\n"
      "  vec3 normal, lightDir;\n"
      "  normal = normalize(gl_NormalMatrix * gl_Normal);\n"
      "  lightDir = normalize(vec3(gl_LightSource[0].position));\n"
      "  shading = abs(dot(normal, lightDir));\n"
      "}\n";

    /*
      Inputs:
        tp && tr - if any components of tp < tr, use color2 (edge color)
        shading  - multiplied by color1. color2 is is without lighting
		*/
    const char *fs_source =
      "uniform vec4 color1, color2;\n"
      "varying vec3 tp, tr, tmp;\n"
      "varying float shading;\n"
      "void main() {\n"
      "  gl_FragColor = vec4(color1.r * shading, color1.g * shading, color1.b * shading, color1.a);\n"
      "  if (tp.x < tr.x || tp.y < tr.y || tp.z < tr.z)\n"
      "    gl_FragColor = color2;\n"
      "}\n";

    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, (const GLchar**)&vs_source, NULL);
    glCompileShader(vs);

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, (const GLchar**)&fs_source, NULL);
    glCompileShader(fs);

    GLuint edgeshader_prog = glCreateProgram();
    glAttachShader(edgeshader_prog, vs);
    glAttachShader(edgeshader_prog, fs);
    glLinkProgram(edgeshader_prog);

    shaderinfo[0] = edgeshader_prog;
    shaderinfo[1] = glGetUniformLocation(edgeshader_prog, "color1");
    shaderinfo[2] = glGetUniformLocation(edgeshader_prog, "color2");
    shaderinfo[3] = glGetAttribLocation(edgeshader_prog, "trig");
    shaderinfo[4] = glGetAttribLocation(edgeshader_prog, "pos_b");
    shaderinfo[5] = glGetAttribLocation(edgeshader_prog, "pos_c");
    shaderinfo[6] = glGetAttribLocation(edgeshader_prog, "mask");
    shaderinfo[7] = glGetUniformLocation(edgeshader_prog, "xscale");
    shaderinfo[8] = glGetUniformLocation(edgeshader_prog, "yscale");

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
      fprintf(stderr, "OpenGL Error: %s\n", gluErrorString(err));
    }

    GLint status;
    glGetProgramiv(edgeshader_prog, GL_LINK_STATUS, &status);
    if (status == GL_FALSE) {
      int loglen;
      char logbuffer[1000];
      glGetProgramInfoLog(edgeshader_prog, sizeof(logbuffer), &loglen, logbuffer);
      fprintf(stderr, "OpenGL Program Linker Error:\n%.*s", loglen, logbuffer);
    } else {
      int loglen;
      char logbuffer[1000];
      glGetProgramInfoLog(edgeshader_prog, sizeof(logbuffer), &loglen, logbuffer);
      if (loglen > 0) {
        fprintf(stderr, "OpenGL Program Link OK:\n%.*s", loglen, logbuffer);
      }
      glValidateProgram(edgeshader_prog);
      glGetProgramInfoLog(edgeshader_prog, sizeof(logbuffer), &loglen, logbuffer);
      if (loglen > 0) {
        fprintf(stderr, "OpenGL Program Validation results:\n%.*s", loglen, logbuffer);
      }
    }
  }
}
#endif

/*
	void initializeGL(); //


	void paintGL(); //
	bool save(const char *filename); //
	//bool save(std::ostream &output); // not implemented in qgl?

	GLint shaderinfo[11];  //

*/
