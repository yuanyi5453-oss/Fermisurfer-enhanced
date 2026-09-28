#ifndef FERMISURFER_VECTOR_CAPTURE_HPP
#define FERMISURFER_VECTOR_CAPTURE_HPP

namespace vector_capture {

/* Set by the vector export path between gl2psBeginPage/gl2psEndPage. */
extern bool active;

/* Replacement for glDrawArrays used while a GL2PS page is active. */
void draw_arrays(unsigned int mode, int first, int count);

}

#endif // FERMISURFER_VECTOR_CAPTURE_HPP
