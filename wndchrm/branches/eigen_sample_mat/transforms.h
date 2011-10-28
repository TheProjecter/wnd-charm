#ifndef __TRANSFORMS_H_
#define __TRANSFORMS_H_

#include <string>

//using namespace std;

class ImageMatrix;

/*! Transform
 *  defines the interface for all inheriting transform classes
 *  Turns any class that inherits this interface into a singleton
 */
class Transform {
	public:
		virtual int transform( ImageMatrix * matrix_IN, ImageMatrix ** matrix_OUT_p ) = 0;
		std::string name;
		void print_info();
	protected:
		Transform() {};
};

class EmptyTransform : public Transform {
	public:
		EmptyTransform (std::string &s) { name = s;}
		EmptyTransform (const char *s) { name = s;}
		EmptyTransform ();
		virtual int    transform( ImageMatrix * matrix_IN, ImageMatrix ** matrix_OUT_p );
};


class FourierTransform : public Transform {
	public:
		FourierTransform();
		virtual int    transform( ImageMatrix * matrix_IN, ImageMatrix ** matrix_OUT_p );
};

class ChebyshevTransform: public Transform {
	public:
		ChebyshevTransform();
		virtual int    transform( ImageMatrix * matrix_IN, ImageMatrix ** matrix_OUT_p );
};

class WaveletTransform : public Transform {
	public:
		WaveletTransform();
		virtual int    transform( ImageMatrix * matrix_IN, ImageMatrix ** matrix_OUT_p );
};

class EdgeTransform : public Transform {
	public:
		EdgeTransform();
		virtual int    transform( ImageMatrix * matrix_IN, ImageMatrix ** matrix_OUT_p );
};

class ColorTransform : public Transform {
	public:
		ColorTransform();
		vector<double> histogram_vals;
		virtual int    transform( ImageMatrix * matrix_IN, ImageMatrix ** matrix_OUT_p );
};

class HueTransform : public Transform {
	public:
		HueTransform();
		virtual int    transform( ImageMatrix * matrix_IN, ImageMatrix ** matrix_OUT_p );
};

	
#define WNDCHARM_REGISTER_TRANSFORM(tform_name) \
struct tform_name##TransformRegistrar \
{ \
  tform_name##TransformRegistrar() \
  { \
    FeatureNames *phonebook = FeatureNames::get_instance(); \
		tform_name *tform_instance = new tform_name; \
		int retval = phonebook->register_transform( tform_instance->name, dynamic_cast<Transform*>( tform_instance ) ); \
		std::cout << "call to register_transform " << #tform_name << " returned " << retval << std::endl; \
  } \
}; \
static tform_name##TransformRegistrar tform_name##TransformRegistrar_instance;

#endif