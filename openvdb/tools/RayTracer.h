///////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2012-2013 DreamWorks Animation LLC
//
// All rights reserved. This software is distributed under the
// Mozilla Public License 2.0 ( http://www.mozilla.org/MPL/2.0/ )
//
// Redistributions of source code must retain the above copyright
// and license notice and the following restrictions and disclaimer.
//
// *     Neither the name of DreamWorks Animation nor the names of
// its contributors may be used to endorse or promote products derived
// from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
// IN NO EVENT SHALL THE COPYRIGHT HOLDERS' AND CONTRIBUTORS' AGGREGATE
// LIABILITY FOR ALL CLAIMS REGARDLESS OF THEIR BASIS EXCEED US$250.00.
//
///////////////////////////////////////////////////////////////////////////
///
/// @file RayTracer.h
///
/// @author Ken Museth
///
/// @brief Defines a multi-threaded and yet simple LevelSetRayTracer,
/// a PerspectiveCamera and an OrthographicCamera (both designed to
/// mimic a Houdini camera), a Film class and some rather naive
/// shaders (just for test-rendering).
///
/// @note These classes are only included as examples of ray tracing
/// of level sets represented in OpenVDB volumes and obviously not for
/// production quality rendering.
///
/// @todo Add a volume renderer 

#ifndef OPENVDB_TOOLS_RAYTRACER_HAS_BEEN_INCLUDED
#define OPENVDB_TOOLS_RAYTRACER_HAS_BEEN_INCLUDED

#include <openvdb/Types.h>
#include <openvdb/math/Ray.h>
#include <openvdb/math/Math.h>
#include <openvdb/tools/RayIntersector.h>
#include <boost/scoped_ptr.hpp>
#include <vector>

// Required by Film::saveEXR
/*
#include <OpenEXR/ImfPixelType.h>
#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfOutputFile.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfFrameBuffer.h>
*/

namespace openvdb {
OPENVDB_USE_VERSION_NAMESPACE
namespace OPENVDB_VERSION_NAME {
namespace tools {

// Forward declarations
class BaseCamera;
class BaseShader;
    
//////////////////////////////////////// RAYTRACER ////////////////////////////////////////

/// @brief Ray-trace a volume.    
template<typename GridT>
inline void rayTrace(const GridT&,
                     const BaseShader&,
                     BaseCamera&,
                     size_t pixelSamples = 1,
                     unsigned int seed = 0,
                     bool threaded = true);

/// @brief Ray-trace a volume using a given ray intersector.
template<typename GridT, typename IntersectorT>
inline void rayTrace(const GridT&,
                     const IntersectorT&,
                     const BaseShader&,
                     BaseCamera&,
                     size_t pixelSamples = 1,
                     unsigned int seed = 0,
                     bool threaded = true);

/// @brief A (very) simple multi-threaded ray tracer specefically for narrow level sets.
/// @warning Primarily included as a reference implementation.
template<typename GridT, typename IntersectorT = tools::LevelSetRayIntersector<GridT> >
class LevelSetRayTracer
{
public:
    typedef GridT                           GridType;
    typedef typename IntersectorT::Vec3Type Vec3Type;
    typedef typename IntersectorT::RayType  RayType;
    
    LevelSetRayTracer(const GridT& grid,
                      const BaseShader& shader,
                      BaseCamera& camera,
                      size_t pixelSamples = 1,
                      unsigned int seed = 0);
    LevelSetRayTracer(const IntersectorT& inter,
                      const BaseShader& shader,
                      BaseCamera& camera,
                      size_t pixelSamples = 1,
                      unsigned int seed = 0);
    LevelSetRayTracer(const LevelSetRayTracer& other);
    ~LevelSetRayTracer();
    void setGrid(const GridT& grid);
    void setIntersector(const IntersectorT& inter);
    void setShader(const BaseShader& shader);
    void setCamera(BaseCamera& camera);
    void setPixelSamples(size_t pixelSamples, unsigned int seed = 0);
    void trace(bool threaded = true);
    void operator()(const tbb::blocked_range<size_t>& range) const;
     
  private:
    
    const bool                          mIsMaster;
    double*                             mRand;
    IntersectorT                        mInter;
    boost::scoped_ptr<const BaseShader> mShader;
    BaseCamera*                         mCamera;
    size_t                              mSubPixels;
};// LevelSetRayTracer
    
//////////////////////////////////////// FILM ////////////////////////////////////////

/// @brief A simple film class used for concurrent updating of pixel
/// values, background initialization and PPM/EXR file output.
class Film
{
public:

    /// @brief Floating-point RGBA components in the range [0, 1].
    /// This is our prefered representation for color processing.
    struct RGBA
    {
        typedef float ValueT;
        RGBA() : r(0), g(0), b(0), a(1) {}
        explicit RGBA(ValueT intensity) : r(intensity), g(intensity), b(intensity), a(1) {}
        explicit RGBA(ValueT _r, ValueT _g, ValueT _b, ValueT _a=1.0f) : r(_r), g(_g), b(_b), a(_a) {}
        inline RGBA  operator* (ValueT scale)  const { return RGBA(r*scale, g*scale, b*scale);}
        inline RGBA  operator+ (const RGBA& rhs) const { return RGBA(r+rhs.r, g+rhs.g, b+rhs.b);}
        inline RGBA  operator* (const RGBA& rhs) const { return RGBA(r*rhs.r, g*rhs.g, b*rhs.b);}
        inline RGBA& operator+=(const RGBA& rhs) { r+=rhs.r; g+=rhs.g; b+=rhs.b, a+=rhs.a; return *this;}
        inline void over(const RGBA& rhs)
        {
            const float s = rhs.a*(1.0f-a);
            r = a*r+s*rhs.r;
            g = a*g+s*rhs.g;
            b = a*b+s*rhs.b;
            a = a + s;
        }   
        ValueT r, g, b, a;
    };
    
    Film(size_t width, size_t height)
        : mWidth(width), mHeight(height), mSize(width*height), mPixels(new RGBA[mSize])
      {
      }
    Film(size_t width, size_t height, const RGBA& bg)
        : mWidth(width), mHeight(height), mSize(width*height), mPixels(new RGBA[mSize])
      {
          this->fill(bg);
      }
    ~Film() { delete mPixels; }

    const RGBA& pixel(size_t w, size_t h) const
    {
        assert(w < mWidth);
        assert(h < mHeight);
        return mPixels[w + h*mWidth];
    }

    RGBA& pixel(size_t w, size_t h)
    {
        assert(w < mWidth);
        assert(h < mHeight);
        return mPixels[w + h*mWidth];
    }

    void fill(const RGBA& rgb=RGBA(0)) { for (size_t i=0; i<mSize; ++i) mPixels[i] = rgb; }
    void checkerboard(const RGBA& c1=RGBA(0.3f), const RGBA& c2=RGBA(0.6f), size_t size=32)
      {
          RGBA *p = mPixels;
          for (size_t j = 0; j < mHeight; ++j) {
              for (size_t i = 0; i < mWidth; ++i, ++p) {
                  *p = ((i & size) ^ (j & size)) ? c1 : c2;
              }
          }
      }

    void savePPM(const std::string& fileName)
    {
        std::string name(fileName + ".ppm");
        unsigned char* tmp = new unsigned char[3*mSize], *q = tmp;
        RGBA* p = mPixels;
        size_t n = mSize;
        while (n--) {
            *q++ = static_cast<unsigned char>(255.0f*(*p  ).r);
            *q++ = static_cast<unsigned char>(255.0f*(*p  ).g);
            *q++ = static_cast<unsigned char>(255.0f*(*p++).b);
        }    
        
        std::ofstream os(name.c_str(), std::ios_base::binary);
        if (!os.is_open()) {
            std::cerr << "Error opening PPM file \"" << name << "\"" << std::endl;
            return;
        }
        
        os << "P6\n" << mWidth << " " << mHeight << "\n255\n";
        os.write((const char *)&(*tmp), 3*mSize*sizeof(unsigned char));
        delete [] tmp;
    }
    /*
    void saveEXR(const std::string& fileName, size_t compression = 2, size_t threads = 8)
    {
        std::string name(fileName + ".exr");

        if (threads>0) Imf::setGlobalThreadCount(threads);
        Imf::Header header(mWidth, mHeight);
        if (compression==0) header.compression() = Imf::NO_COMPRESSION;
        if (compression==1) header.compression() = Imf::RLE_COMPRESSION;
        if (compression>=2) header.compression() = Imf::ZIP_COMPRESSION;
        header.channels().insert("R", Imf::Channel(Imf::FLOAT));
        header.channels().insert("G", Imf::Channel(Imf::FLOAT));
        header.channels().insert("B", Imf::Channel(Imf::FLOAT));
        header.channels().insert("A", Imf::Channel(Imf::FLOAT));
        
        Imf::FrameBuffer framebuffer;
        framebuffer.insert("R", Imf::Slice( Imf::FLOAT, (char *) &(mPixels[0].r),
                                            sizeof (RGBA), sizeof (RGBA) * mWidth));
        framebuffer.insert("G", Imf::Slice( Imf::FLOAT, (char *) &(mPixels[0].g),
                                            sizeof (RGBA), sizeof (RGBA) * mWidth));
        framebuffer.insert("B", Imf::Slice( Imf::FLOAT, (char *) &(mPixels[0].b),
                                            sizeof (RGBA), sizeof (RGBA) * mWidth));
        framebuffer.insert("A", Imf::Slice( Imf::FLOAT, (char *) &(mPixels[0].a),
                                            sizeof (RGBA), sizeof (RGBA) * mWidth));
        
        Imf::OutputFile file(name.c_str(), header);
        file.setFrameBuffer(framebuffer);
        file.writePixels(mHeight);
    }
    */
    size_t width()       const { return mWidth; }
    size_t height()      const { return mHeight; }
    size_t numPixels()   const { return mSize; }
    const RGBA* pixels() const { return mPixels; }

private:
    size_t mWidth, mHeight, mSize;
    RGBA*  mPixels;
};// Film
    
//////////////////////////////////////// CAMERAS ////////////////////////////////////////

// Abstract base class for the perspective and orthographic cameras
class BaseCamera
{
  public:
    BaseCamera(Film& film, const Vec3R& rotation, const Vec3R& translation,
               double frameWidth, double nearPlane, double farPlane)
        :  mFilm(&film), mScaleWidth(frameWidth),
           mScaleHeight(frameWidth*film.height()/double(film.width())),
           mRay(Vec3R(0.0), Vec3R(1.0), nearPlane, farPlane)
    {
        assert(nearPlane > 0 && farPlane > nearPlane);
        mScreenToWorld.accumPostRotation(math::X_AXIS, rotation[0] * M_PI / 180.0);
        mScreenToWorld.accumPostRotation(math::Y_AXIS, rotation[1] * M_PI / 180.0);
        mScreenToWorld.accumPostRotation(math::Z_AXIS, rotation[2] * M_PI / 180.0);
        mScreenToWorld.accumPostTranslation(translation);
        mRay.setEye(mScreenToWorld.applyMap(Vec3R(0.0)));
        mRay.setDir(mScreenToWorld.applyJacobian(Vec3R(0.0, 0.0, -1.0)));
    }
    virtual ~BaseCamera() {}
    inline Film::RGBA& pixel(size_t i, size_t j) { return mFilm->pixel(i, j); }
    inline size_t width()  const { return mFilm->width(); }
    inline size_t height() const { return mFilm->height(); }
    inline Vec3R rasterToScreen(double i, double j, double z) const
    {
        return Vec3R( (2 * i / mFilm->width() - 1)  * mScaleWidth,
                      (1 - 2 * j / mFilm->height()) * mScaleHeight, z );
    }
    virtual math::Ray<double> getRay(size_t i, size_t j,
                                     double iOffset=0.5, double jOffset=0.5) const = 0;
protected:
    Film*  mFilm;
    double mScaleWidth, mScaleHeight;
    math::Ray<double> mRay;
    math::AffineMap   mScreenToWorld;
};// BaseCamera

class PerspectiveCamera : public BaseCamera
{
  public:
    /// @brief Constructor
    /// @param film The film (i.e. image) defining the pixel resolution.
    /// @param rotation Rotation in degrees placing the camera in
    /// world space. Note they are applies in lexicographic order.
    /// @param translation Translation in world units placing the
    /// camera in world space. Note translation is applied after rotation. 
    /// @param focalLength  Focal length of the camera in mm.
    /// The default value of 50mm corrseponds to that of Houdini's camera.
    /// @param aperture The width in mm of the visible field, i.e. frame.  
    /// The default value of 41.2136 mm corrseponds to that of
    /// Houdini's camera.
    /// @param nearPlane Defines the near clipping plane as an offset
    /// in world units of the screen plane.
    /// @param farPlane  Defines the far clipping plane as an offset
    /// in world units of the screen plane.
    ///
    /// @note If no rotaion and translation is provided the camera is
    /// placed in world space at (0,0,0) and points in the direction
    /// of the negative z-axis.
    PerspectiveCamera(Film& film,
                      const Vec3R& rotation    = Vec3R(0.0),
                      const Vec3R& translation = Vec3R(0.0),
                      double focalLength = 50.0,
                      double aperture    = 41.2136,
                      double nearPlane   = 1e-3,
                      double farPlane    = std::numeric_limits<double>::max())
        : BaseCamera(film, rotation, translation, 0.5*aperture/focalLength, nearPlane, farPlane)
    {
    }
    virtual ~PerspectiveCamera() {}
    /// @brief Return a Ray in world space given the pixel indices and
    /// optional offsets in the range [0,1]. An offset of 0.5 corresponds 
    /// to the center of the pixel.
    math::Ray<double> getRay(size_t i, size_t j,
                             double iOffset=0.5, double jOffset=0.5) const
    {
        math::Ray<double> ray(mRay);
        Vec3R dir = BaseCamera::rasterToScreen(i + iOffset, j + jOffset, -1.0);
        dir = BaseCamera::mScreenToWorld.applyJacobian(dir);
        dir.normalize();
        ray.scaleTime(1.0/dir.dot(ray.dir()));
        ray.setDir(dir);
        return ray;
    }
    /// @brief Return horizontal field of view in degrees given a
    /// focal lenth in mm and assuming the specified aperture in mm.
    inline static double focalLengthToFieldOfView(double length, double aperture)
    {
        return 360.0 / M_PI * atan(aperture/(2.0*length));
    }
    /// @brief Return Focal length in mm given a horizontal field of
    /// view in degrees and assuming the specified aperture in mm.
    inline static double fieldOfViewToFocalLength(double fov, double aperture)
    {
        return aperture/(2.0*(tan(fov * M_PI / 360.0)));
    }
};// PerspectiveCamera

class OrthographicCamera : public BaseCamera
{
  public:
    /// @brief Constructor
    /// @param film The film (i.e. image) defining the pixel resolution.
    /// @param rotation Rotation in degrees placing the camera in
    /// world space. Note they are applies in lexicographic order.
    /// @param translation Translation in world units placing the
    /// camera in world space. Note translation is applied after rotation. 
    /// @param frameWidth The width in of the frame in world units.
    /// @param nearPlane Defines the near clipping plane as an offset
    /// in world units of the screen plane.
    /// @param farPlane  Defines the far clipping plane as an offset
    /// in world units of the screen plane.
    ///
    /// @note If no rotaion and translation is provided the camera is
    /// placed in world space at (0,0,0) and points in the direction
    /// of the negative z-axis.
    OrthographicCamera(Film& film,
                       const Vec3R& rotation    = Vec3R(0.0),
                       const Vec3R& translation = Vec3R(0.0),
                       double frameWidth = 1.0,
                       double nearPlane  = 1e-3,
                       double farPlane   = std::numeric_limits<double>::max())
        : BaseCamera(film, rotation, translation, 0.5*frameWidth, nearPlane, farPlane)
    {
    }
    virtual ~OrthographicCamera() {}
    /// @brief Return a Ray in world space given the pixel indices and
    /// optional offsets in the range [0,1]. An offset of 0.5 corresponds 
    /// to the center of the pixel.
    math::Ray<double> getRay(size_t i, size_t j,
                             double iOffset=0.5, double jOffset=0.5) const
    {
        math::Ray<double> ray(mRay);
        Vec3R eye = BaseCamera::rasterToScreen(i + iOffset, j + jOffset, 0.0);
        ray.setEye(BaseCamera::mScreenToWorld.applyMap(eye));
        return ray;
    }
};// OrthographicCamera
    
//////////////////////////////////////// SHADERS ////////////////////////////////////////

/// Abstract base class for the shaders
class BaseShader
{
public :
    typedef math::Ray<Real> RayT;
    BaseShader() {}
    virtual ~BaseShader() {}
    virtual Film::RGBA operator()(const Vec3R&, const Vec3R&, const RayT&) const = 0;
    virtual BaseShader* copy() const = 0;
};

/// Produces a simple matte
class MatteShader : public BaseShader
{
  public:
    MatteShader(const Film::RGBA& c = Film::RGBA(1.0f)) : mRGBA(c) {}
    virtual ~MatteShader() {}
    inline Film::RGBA operator()(const Vec3R&, const Vec3R&,
                                 const BaseShader::RayT&) const { return mRGBA; }
    BaseShader* copy() const { return new MatteShader(*this); }
  private:
    const Film::RGBA mRGBA;
};

/// Color shading that treats the normal (x, y, z) values as (r, g, b) color components.
class NormalShader : public BaseShader
{
public:
    NormalShader(const Film::RGBA& c = Film::RGBA(1.0f)) : mRGBA(c*0.5f) {}
    virtual ~NormalShader() {}
    inline Film::RGBA operator()(const Vec3R&, const Vec3R& normal,
                                 const BaseShader::RayT&) const
    {
        return mRGBA*Film::RGBA(normal[0]+1.0f, normal[1]+1.0f, normal[2]+1.0f);
    }
    BaseShader* copy() const { return new NormalShader(*this); }
  private:
    const Film::RGBA mRGBA;
};

// This is a simple diffuse lambertian surface shader. Diffuse simply
// means the color is constant (e.g. white) and Lambertian implies
// that the (radiant) intensity is directly proportional to the cosine
// of the angle between the surface normal and the direction of the
// light source.
class DiffuseShader : public BaseShader
{
  public:
    DiffuseShader(const Film::RGBA& d = Film::RGBA(1.0f)) : mRGBA(d) {}
    inline Film::RGBA operator()(const Vec3R&, const Vec3R& normal,
                                 const BaseShader::RayT& ray) const
      {
          // We assume a single directional light source at the camera so
          // cosine of the angle between the surface normal and the
          // direction of the light source becomes the dot product of the
          // surface normal and inverse direction of the ray! We will also 
          // ignore negative dot products cooresponding to strict one-sided shading!
          //return mRGBA * math::Max(0.0, normal.dot(-ray.dir()));
          
          // We take the abs of the dot product corresponding to
          // having light sources at +/- ray.dir(), i.e. two-sided shading!
          return mRGBA * math::Abs(normal.dot(ray.dir()));
      }
    BaseShader* copy() const { return new DiffuseShader(*this); }
  private:
    const Film::RGBA mRGBA;
};


//////////////////////////////////////// RAYTRACER ////////////////////////////////////////


template<typename GridT>
inline void rayTrace(const GridT& grid,
                     const BaseShader& shader,
                     BaseCamera& camera,
                     size_t pixelSamples,
                     unsigned int seed,
                     bool threaded)
{
    LevelSetRayTracer<GridT, tools::LevelSetRayIntersector<GridT> > tracer(grid, shader, camera, pixelSamples, seed);
    tracer.trace(threaded);
}


template<typename GridT, typename IntersectorT>
inline void rayTrace(const GridT&,
                     const IntersectorT& inter,
                     const BaseShader& shader,
                     BaseCamera& camera,
                     size_t pixelSamples,
                     unsigned int seed,
                     bool threaded)
{
    LevelSetRayTracer<GridT, IntersectorT> tracer(inter, shader, camera, pixelSamples, seed);
    tracer.trace(threaded);
}


////////////////////////////////////////


template<typename GridT, typename IntersectorT>
inline LevelSetRayTracer<GridT, IntersectorT>::
LevelSetRayTracer(const GridT& grid,
                  const BaseShader& shader,
                  BaseCamera& camera,
                  size_t pixelSamples,
                  unsigned int seed)
    : mIsMaster(true),
      mRand(NULL),
      mInter(grid),
      mShader(shader.copy()),
      mCamera(&camera)
{
    this->setPixelSamples(pixelSamples, seed);
}

template<typename GridT, typename IntersectorT>
inline LevelSetRayTracer<GridT, IntersectorT>::
LevelSetRayTracer(const IntersectorT& inter,
                  const BaseShader& shader,
                  BaseCamera& camera,
                  size_t pixelSamples,
                  unsigned int seed)
    : mIsMaster(true),
      mRand(NULL),
      mInter(inter),
      mShader(shader.copy()),
      mCamera(&camera)
{
    this->setPixelSamples(pixelSamples, seed);
}

template<typename GridT, typename IntersectorT>
inline LevelSetRayTracer<GridT, IntersectorT>::
LevelSetRayTracer(const LevelSetRayTracer& other) :
    mIsMaster(false),
    mRand(other.mRand),
    mInter(other.mInter),
    mShader(other.mShader->copy()),
    mCamera(other.mCamera),
    mSubPixels(other.mSubPixels)
{
}

template<typename GridT, typename IntersectorT>
inline LevelSetRayTracer<GridT, IntersectorT>::
~LevelSetRayTracer()
{
    if (mIsMaster) delete [] mRand;
}

template<typename GridT, typename IntersectorT>
inline void LevelSetRayTracer<GridT, IntersectorT>::
setGrid(const GridT& grid)
{
    assert(mIsMaster);
    mInter = IntersectorT(grid);
}

template<typename GridT, typename IntersectorT>
inline void LevelSetRayTracer<GridT, IntersectorT>::
setIntersector(const IntersectorT& inter)
{
    assert(mIsMaster);
    mInter = inter;
}    

template<typename GridT, typename IntersectorT>
inline void LevelSetRayTracer<GridT, IntersectorT>::
setShader(const BaseShader& shader)
{
    assert(mIsMaster);
    mShader.reset(shader.copy());
}

template<typename GridT, typename IntersectorT>
inline void LevelSetRayTracer<GridT, IntersectorT>::
setCamera(BaseCamera& camera)
{
    assert(mIsMaster);
    mCamera = &camera;
}

template<typename GridT, typename IntersectorT>
inline void LevelSetRayTracer<GridT, IntersectorT>::
setPixelSamples(size_t pixelSamples, unsigned int seed)
{
    assert(mIsMaster);
    assert(pixelSamples>0);
    mSubPixels = pixelSamples - 1;
    delete [] mRand;
    if (mSubPixels > 0) {
        mRand = new double[16];
        math::Rand01<double> rand(seed);//offsets for anti-aliaing by jittered super-sampling
        for (size_t i=0; i<16; ++i) mRand[i] = rand();
    } else {
        mRand = NULL;
    }
}    

template<typename GridT, typename IntersectorT>
inline void LevelSetRayTracer<GridT, IntersectorT>::
trace(bool threaded)
{   
    tbb::blocked_range<size_t> range(0, mCamera->height());
    threaded ? tbb::parallel_for(range, *this) : (*this)(range);
}

template<typename GridT, typename IntersectorT>
inline void LevelSetRayTracer<GridT, IntersectorT>::
operator()(const tbb::blocked_range<size_t>& range) const
{
    Vec3Type xyz, nml;
    const float frac = 1.0f / (1.0f + mSubPixels);
    for (size_t j=range.begin(), n=0, je = range.end(); j<je; ++j) {
        for (size_t i=0, ie = mCamera->width(); i<ie; ++i) {
            Film::RGBA& bg = mCamera->pixel(i,j);
            RayType ray = mCamera->getRay(i, j);//primary ray
            Film::RGBA c = mInter.intersectsWS(ray, xyz, nml) ? (*mShader)(xyz, nml, ray) : bg;
            for (size_t k=0; k<mSubPixels; ++k, n +=2 ) {
                ray = mCamera->getRay(i, j, mRand[n & 15], mRand[n+1 & 15]);
                c += mInter.intersectsWS(ray, xyz, nml) ? (*mShader)(xyz, nml, ray) : bg;
            }//loop over sub-pixels
            bg = c*frac;
        }//loop over image height
    }//loop over image width
}

} // namespace tools
} // namespace OPENVDB_VERSION_NAME
} // namespace openvdb

#endif // OPENVDB_TOOLS_RAYTRACER_HAS_BEEN_INCLUDED

// Copyright (c) 2012-2013 DreamWorks Animation LLC
// All rights reserved. This software is distributed under the
// Mozilla Public License 2.0 ( http://www.mozilla.org/MPL/2.0/ )
