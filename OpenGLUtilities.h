//
//  OpenGLUtilities.h
//  OpenGLUtilities
//
//  Created by Chris Birkhold on 8/19/18.
//  Copyright © 2018 Chris Birkhold. All rights reserved.
//

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <vector>
#include <string>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#include <OpenGL/gl3ext.h>
#elif defined(_WIN32)
#include <GL/glew.h>
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace toolbox {

  ////////////////////////////////////////////////////////////////////////////////
  ////////////////////////////////////////////////////////////////////////////////

  //------------------------------------------------------------------------------
  // Utility functions for creating and using OpenGL shaders.
  //------------------------------------------------------------------------------

  class OpenGLShader
  {
  public:

    static GLuint create_from_source(GLenum type, const std::string& source);
  };

  ////////////////////////////////////////////////////////////////////////////////
  ////////////////////////////////////////////////////////////////////////////////

  //------------------------------------------------------------------------------
  // Utility functions for creating and using OpenGL programs.
  //------------------------------------------------------------------------------

  class OpenGLProgram
  {
  public:

    typedef std::vector<std::tuple<GLint, GLint, std::string>> frag_data_location_list_t;
    typedef std::vector<std::tuple<GLint, std::string>> attribute_location_list_t;
    typedef std::vector<std::tuple<GLint, std::string>> uniform_location_list_t;

    //------------------------------------------------------------------------------
    // Create a program from the given shaders, bind the given attribute and
    // fragment data locations and link the program. A valid program is returned or
    // an exception thrown.
    //
    // Attribute locations are bound to an index by name. The zero-based index must
    // be less than GL_MAX_VERTEX_ATTRIBS. Binding will only occur for valid
    // parameters and if no in-shader layout specification was provided. A list of
    // all active vertex attributes is returned.
    //
    // Fragment data locations are bound to a location and index by name. If the
    // index is zero (default) the zero-based location must be less than
    // GL_MAX_DRAW_BUFFERS and if index is one the lcoation must be less than
    // GL_MAX_DUAL_SOURCE_DRAW_BUFFERS. Binding will only occur for valid parameters
    // and if no in-shader layout specification was provided. The actually bound
    // locations and indices are returned for all given (and only those) valid
    // names. If the location or index was invalid no binding is attempted but the
    // actually bound location is still returned if the name is valid. OpenGL does
    // not provide a way to enumerate fragment data locations so names that are not
    // provided can not be queried and returned.
    static GLuint create_from_shaders(GLuint vertex_shader,
                                      GLuint fragment_shader,
                                      attribute_location_list_t& attribute_locations,
                                      frag_data_location_list_t& frag_data_locations);

    //------------------------------------------------------------------------------
    // Validate the program within the current OpenGL state, usually just before a
    // draw call is made. This can be costly and should be reserved for debugging.
    static bool validate(GLuint program);
  };

  ////////////////////////////////////////////////////////////////////////////////
  ////////////////////////////////////////////////////////////////////////////////

} // namespace toolbox

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
