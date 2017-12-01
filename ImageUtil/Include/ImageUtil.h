#pragma once

#include <unordered_map>
#include <Image.h>
#include "PixelUtil.h"
#include "../../LLUtils/Include/Rect.h"
#include "half.hpp"
#include "HSLRGB.h"


namespace IMUtil
{
#define RGBA(R,G,B,A) (A << 24 | B << 16 | G << 8 | R ) 
#define RGBA_GRAYSCALE(X) (RGBA(X,X,X,255))
#define RGBA_To_GRAYSCALE_LUMA(R,G,B,A) (RGBA((int)(R * 0.2126), (int)(G * 0.7152) ,(int)(B * 0.0722) ,255))

    class ImageUtil
    {
    private:
        struct ConvertKey
        {
            
            struct Hash
            {
                std::size_t operator()(const ConvertKey& key) const
                {
                    using eumType = std::underlying_type<IMCodec::TexelFormat>::type;
                    static_assert(sizeof(eumType) <= 2, "Size of enum underlying type should not be more than two bytes");
                    return static_cast<eumType>(key.source) << (sizeof(eumType) * 8) | static_cast<eumType>(key.target);
                }
            };

            ConvertKey(IMCodec::TexelFormat aSource, IMCodec::TexelFormat aTarget)
            {
                source = aSource;
                target = aTarget;
            }

            bool operator==(const ConvertKey& rhs) const
            {
                return source == rhs.source && target == rhs.target;
            }

            IMCodec::TexelFormat source;
            IMCodec::TexelFormat target;
        };

        typedef std::unordered_map<ConvertKey, PixelConvertFunc, ConvertKey::Hash> MapConvertKeyToFunc;
        static MapConvertKeyToFunc sConvertionFunction;

    public:

        static IMCodec::ImageSharedPtr Transform(AxisAlignedRTransform transform, IMCodec::ImageSharedPtr image)
        {
            using namespace std;
            using namespace IMCodec;

            if (image->GetIsByteAligned() == false)
                throw std::logic_error("OIV::Image::Transom works only with byte aligned image formats");

            if (transform != AxisAlignedRTransform::None)
            {
                uint8_t* dest = new uint8_t[image->GetTotalSizeOfImageTexels()];

                const size_t MegaBytesPerThread = 6;
                static const uint8_t MaxGlobalThrads = 32;
                static uint8_t maxThreads = static_cast<uint8_t>(
                    min(static_cast<unsigned int>(MaxGlobalThrads), max(1u, thread::hardware_concurrency() - 1)));
                static thread threads[MaxGlobalThrads];


                const size_t bytesPerThread = MegaBytesPerThread * 1024 * 1024;
                const uint8_t totalThreads = (std::min)(maxThreads, static_cast<uint8_t>(image->GetTotalSizeOfImageTexels() / bytesPerThread));
                PixelUtil::TransformTexelsInfo descTemplate;
                descTemplate.transform = transform;
                descTemplate.dstBuffer = dest;
                descTemplate.srcBuffer = image->GetBuffer();
                descTemplate.width = image->GetWidth();
                descTemplate.height = image->GetHeight();
                descTemplate.bytesPerTexel = image->GetBytesPerTexel();
                descTemplate.srcRowPitch = image->GetRowPitchInBytes();
                descTemplate.startCol = 0;
                descTemplate.endCol = image->GetWidth();
                descTemplate.startRow = 0;
                descTemplate.endRow = image->GetHeight();

                if (totalThreads > 0)
                {
                    size_t rowsPerThread = image->GetHeight() / totalThreads;
                    for (uint8_t threadNum = 0; threadNum < totalThreads; threadNum++)
                    {

                        threads[threadNum] = std::thread
                        (
                            [&descTemplate, rowsPerThread, threadNum]()
                        {
                            PixelUtil::TransformTexelsInfo desc = descTemplate;
                            desc.startRow = rowsPerThread * threadNum;
                            desc.endRow = rowsPerThread * (threadNum + 1);
                            PixelUtil::TransformTexels(desc);
                        }
                        );
                    }

                    PixelUtil::TransformTexelsInfo desc = descTemplate;
                    desc.startRow = rowsPerThread * totalThreads;
                    desc.endRow = image->GetHeight();

                    PixelUtil::TransformTexels(desc);

                    for (uint8_t i = 0; i < totalThreads; i++)
                        threads[i].join();
                }
                else
                {
                    // single (main) thread implementation
                    PixelUtil::TransformTexels(descTemplate);
                }

                ImageProperies transformedProperties = image->GetProperties();

                if (transform == AxisAlignedRTransform::Rotate90CW || transform == AxisAlignedRTransform::Rotate90CCW)
                    swap(transformedProperties.Height, transformedProperties.Width);

                transformedProperties.RowPitchInBytes = transformedProperties.Width * image->GetBytesPerTexel();
                transformedProperties.ImageBuffer = dest;
                return ImageSharedPtr(new Image(transformedProperties, image->GetData()));
            }
            else
                return image;
            
        }
        
        static IMCodec::ImageSharedPtr Normalize(IMCodec::ImageSharedPtr image)
        {
            using namespace IMCodec;

            if (image->GetIsByteAligned() == false)
                throw std::logic_error("Can not normalize a non byte aligned pixel format");

            ImageProperies normalizedImageProperties;

            if (image->GetIsRowPitchNormalized() == true)
                throw std::logic_error("Image already normalized");


            const uint8_t* oldBuffer = image->GetBuffer();
            normalizedImageProperties = image->GetProperties();
            uint32_t targetRowPitch = image->GetBytesPerRowOfPixels();
            uint8_t* newBuffer = new uint8_t[image->GetTotalSizeOfImageTexels()];
            normalizedImageProperties.ImageBuffer = newBuffer;
            for (std::size_t y = 0; y < image->GetHeight(); y++)
                for (std::size_t x = 0; x < targetRowPitch; x++)
                {
                    std::size_t srcIndex = y * image->GetRowPitchInBytes() + x;
                    std::size_t dstIndex = y * targetRowPitch + x;

                    newBuffer[dstIndex] = oldBuffer[srcIndex];
                }

            normalizedImageProperties.ImageBuffer = newBuffer;
            normalizedImageProperties.RowPitchInBytes = targetRowPitch;

            
            return ImageSharedPtr(new Image(normalizedImageProperties, image->GetData()));
        }

        static IMCodec::ImageSharedPtr Convert(IMCodec::ImageSharedPtr sourceImage, IMCodec::TexelFormat targetPixelFormat)
        {
            using namespace IMCodec;
            ImageSharedPtr convertedImage;

            if (sourceImage->GetImageType() != targetPixelFormat)
            {
                
                auto converter = sConvertionFunction.find(ConvertKey(sourceImage->GetImageType(), targetPixelFormat));

                if (converter != sConvertionFunction.end())
                {
                    uint8_t targetPixelSize = GetTexelFormatSize(targetPixelFormat);
                    uint8_t* dest = nullptr;
                    
                    //TODO: convert without normalization.
                    convertedImage = sourceImage->GetIsRowPitchNormalized() == true ? sourceImage : Normalize(sourceImage);
                        
                    PixelUtil::Convert(converter->second
                        , &dest
                        , convertedImage->GetBuffer()
                        , targetPixelSize
                        , convertedImage->GetTotalPixels());

                    ImageProperies properties = convertedImage->GetProperties();
                    properties.TexelFormatDecompressed = targetPixelFormat;
                    properties.ImageBuffer = dest;
                    properties.RowPitchInBytes = convertedImage->GetRowPitchInTexels() * (targetPixelSize / 8);
                    return ImageSharedPtr(new Image(properties, convertedImage->GetData()));
                }
            }
            else
            { // No need to convert, return source image.
                convertedImage = sourceImage;

            }
            return convertedImage;
        }

        static IMCodec::ImageSharedPtr GetSubImage(IMCodec::ImageSharedPtr sourceImage, LLUtils::RectI32 subimage)
        {
            LLUtils::RectI32 image = { { 0,0 } ,{ static_cast<int32_t> (sourceImage->GetWidth())
                , static_cast<int32_t> (sourceImage->GetHeight()) } };
            if (subimage.IsNonNegative() && subimage.IsInside(image))
            {
                const uint8_t* sourceBuffer = sourceImage->GetBuffer();

                const uint32_t destBufferSize = subimage.GetWidth() * subimage.GetHeight() * sourceImage->GetBytesPerTexel();
                uint8_t* destBuffer = new uint8_t[destBufferSize];

                for (int32_t y = 0; y < subimage.GetHeight(); y++)
                {
                    for (int32_t x = 0; x < subimage.GetWidth(); x++)
                    {
                        decltype(subimage)::Point_Type topLeft = subimage.GetCorner(LLUtils::Corner::TopLeft);
                        
                        const uint32_t idxDest = y * subimage.GetWidth() + x;
                        const uint32_t idxSource = (y + topLeft.y)  * sourceImage->GetWidth() + (x + topLeft.x);
                        PixelUtil::CopyTexel<PixelUtil::BitTexel32>(destBuffer, idxDest, sourceBuffer, idxSource);
                    }
                }

                using namespace IMCodec;
                ImageProperies props = sourceImage->GetProperties();

                props.Height = subimage.GetHeight();
                props.Width = subimage.GetWidth();
                props.ImageBuffer = destBuffer;
                props.RowPitchInBytes = subimage.GetWidth() * sourceImage->GetBytesPerTexel();

                ImageSharedPtr subImagePtr = ImageSharedPtr(new Image(props, ImageData()));

                return subImagePtr;
            }
            return IMCodec::ImageSharedPtr();
        }

        enum class NormalizeMode
        {
              Default    = 0
            , GrayScale  = 1
            , RainBow    = 2
        };

        template <class T>
        static IMCodec::ImageSharedPtr Normalize(IMCodec::ImageSharedPtr sourceImage, IMCodec::TexelFormat targetPixelFormat, NormalizeMode normalizeMode = NormalizeMode::Default)
        {
            const T* sampleData = reinterpret_cast<const T*> (sourceImage->GetConstBuffer());

            T min = std::numeric_limits<T>::max();
            T max = std::numeric_limits<T>::min();
            
            uint32_t totalPixels = sourceImage->GetTotalPixels();
            for (uint32_t i = 0 ; i < totalPixels ;i++)
            {
                T currentSample = sampleData[i];
                min = std::min(min, currentSample);
                max = std::max(max, currentSample);
            }

            IMCodec::ImageProperies props;
            props = sourceImage->GetProperties();
            props.TexelFormatDecompressed = IMCodec::TexelFormat::I_R8_G8_B8_A8;
            props.RowPitchInBytes = IMCodec::GetTexelFormatSize(IMCodec::TexelFormat::I_R8_G8_B8_A8) / 8 * props.Width;
            props.ImageBuffer = new uint8_t[props.RowPitchInBytes * props.Height];
            
            PixelUtil::BitTexel32Ex* currentTexel = reinterpret_cast<PixelUtil::BitTexel32Ex*>(props.ImageBuffer);


            float length = max - min;

            for (uint32_t i = 0; i < totalPixels; i++)
            {
                const T currentSample = sampleData[i];

                switch (normalizeMode)
                {
                case NormalizeMode::Default:
                case NormalizeMode::GrayScale:
                {
                    uint8_t grayValue = std::min(static_cast<uint8_t>(std::round((currentSample / length) * 255)), static_cast<uint8_t>(255));
                    currentTexel[i].value = RGBA_GRAYSCALE(grayValue);
                }
                    break;

                case NormalizeMode::RainBow:
                {
                    RGB rgb = RGB(0, 0, 0);
                    if (currentSample != 0)
                    {
                        int hue = static_cast<int>(((currentSample / length) * 300) + 240) % 360;
                        rgb = HSLToRGB(HSL(hue, 0.5, 0.5));

                    }
                    currentTexel[i].value = RGBA(rgb.R, rgb.G, rgb.B, 255);
                }
                    break;
                }
             
            }
            return IMCodec::ImageSharedPtr(new IMCodec::Image(props,sourceImage->GetData()));
        }
    };
}
