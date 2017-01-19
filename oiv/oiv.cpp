#pragma warning(disable : 4275 ) // disables warning 4275, pop and push from exceptions
#pragma warning(disable : 4251 ) // disables warning 4251, the annoying warning which isn't needed here...
#include "oiv.h"
#include "External\easyexif\exif.h"
#include "Interfaces\IRenderer.h"
#include "NullRenderer.h"
#include <ImageLoader.h>
#include <ImageUtil.h>
#include "Configuration.h"


#if OIV_BUILD_RENDERER_D3D11 == 1
#include "../OIVD3D11Renderer/OIVD3D11RendererFactory.h"
#endif


#if OIV_BUILD_RENDERER_GL == 1
#include "../OIVGLRenderer/OIVGLRendererFactory.h"
#endif



namespace OIV
{

    #pragma region ZoomScrollStateListener
    LLUtils::PointI32 OIV::GetImageSize()
    {
        using namespace LLUtils;
        return GetDisplayImage() ? PointI32(
                static_cast<PointI32::point_type>(GetDisplayImage()->GetWidth()),
                static_cast<PointI32::point_type>(GetDisplayImage()->GetHeight()))
            : PointI32::Zero;
    }

    LLUtils::PointI32 OIV::GetClientSize()
    {
        return LLUtils::PointI32(fClientWidth, fClientHeight);
    }
    
    void OIV::NotifyDirty()
    {
        Refresh();
    }

    
#pragma endregion 
   

    IMCodec::ImageSharedPtr OIV::GetActiveImage() const
    {
        return fImageManager.GetImage(fActiveHandle);
    }

    IMCodec::ImageSharedPtr OIV::GetDisplayImage() const
    {
        return fDisplayedImage;
    }

    void OIV::UpdateGpuParams()
    {
        LLUtils::PointF64 uvScaleFixed = fScrollState.GetARFixedUVScale();
        LLUtils::PointF64 uvOffset = fScrollState.GetOffset();
        if (GetActiveImage() == nullptr)
            uvScaleFixed = LLUtils::PointF64(1000000, 100000);
        
        fViewParams.showGrid = fShowGrid;
        fViewParams.uViewportSize = GetClientSize();
        fViewParams.uvOffset = uvOffset;
        fViewParams.uvscale = uvScaleFixed;
        
        fRenderer->SetViewParams(fViewParams);
    }

    void OIV::HandleWindowResize()
    {
        if (IsImageDisplayed())
            fScrollState.Refresh();
    }

    bool OIV::IsImageDisplayed() const
    {
        return GetDisplayImage() != nullptr;
    }
    

    OIV_AxisAlignedRTransform OIV::ResolveExifRotation(unsigned short exifRotation) const
    {
        OIV_AxisAlignedRTransform rotation;
            switch (exifRotation)
            {
            case 3:
                rotation = AAT_Rotate180;
                break;
            case 6:
                rotation = AAT_Rotate90CW;
                break;
            case 8:
                rotation = AAT_Rotate90CCW;
                break;
            default:
                rotation = AAT_None;
            }
            return rotation;
    }

    IRendererSharedPtr OIV::CreateBestRenderer()
    {

#ifdef _MSC_VER 
     // Prefer Direct3D11 for windows.
    #if OIV_BUILD_RENDERER_D3D11 == 1
            return D3D11RendererFactory::Create();
    // Prefer Direct3D11 for windows.
    #elif  OIV_BUILD_RENDERER_GL == 1
        return GLRendererFactory::Create();
    #elif OIV_ALLOW_NULL_RENDERER == 1
        return IRendererSharedPtr(new NullRenderer());
    #else
        #error No valid renderers detected.
    #endif

#else // _MSC_VER
// If no windows choose GL renderer
    #if OIV_BUILD_RENDERER_GL == 1
        return GLRendererFactory::Create();
    #elif OIV_ALLOW_NULL_RENDERER == 1
        return IRendererSharedPtr(new NullRenderer());
    #else
        #error No valid renderers detected.
    #endif
#endif

        throw std::runtime_error("wrong build configuration");

    }


    //int OIV::DisplayImage(ImageHandle handle)
    //{
    //    fDisplayedImage = fOpenedImage;

    //    using namespace easyexif;
    //    EXIFInfo exifInfo;

    //    if (exifInfo.parseFrom(static_cast<const unsigned char*>(buffer), static_cast<unsigned int>(size)) == PARSE_EXIF_SUCCESS)
    //        fDisplayedImage = IMUtil::ImageUtil::Transform(
    //            static_cast<IMUtil::AxisAlignedRTransform>(ResolveExifRotation(exifInfo.Orientation))
    //            , fDisplayedImage);

    //    fDisplayedImage = IMUtil::ImageUtil::Convert(fDisplayedImage, TF_I_R8_G8_B8_A8);

    //    if (fDisplayedImage != nullptr)
    //    {
    //        if (fRenderer->SetImage(fDisplayedImage) == RC_Success)
    //        {
    //            fScrollState.Reset(true);
    //            resultCode = RC_Success;
    //        }
    //        else
    //        {
    //            resultCode = RC_PixelFormatConversionFailed;
    //        }
    //    }
    //}


#pragma region IPictureViewer implementation
    // IPictureViewr implementation
    int OIV::LoadFile(void* buffer, std::size_t size, char* extension, bool onlyRegisteredExtension, ImageHandle& handle)
    {
        ResultCode resultCode = RC_UknownError;
        using namespace IMCodec;
        ImageSharedPtr image = ImageSharedPtr(fImageLoader.Load(static_cast<uint8_t*>(buffer), size, extension, onlyRegisteredExtension));

        if (image != nullptr)
        {
            handle = fImageManager.AddImage(image);
            resultCode = RC_Success;
        }
            
        return resultCode;
    }

    IMCodec::ImageSharedPtr OIV::ApplyExifRotation(IMCodec::ImageSharedPtr image) const
    {
        return IMUtil::ImageUtil::Transform(
            static_cast<IMUtil::AxisAlignedRTransform>(ResolveExifRotation(image->GetData().exifOrientation))
            , image);
    }

    ResultCode OIV::DisplayFile(const ImageHandle handle, const OIV_CMD_DisplayImage_Flags display_flags)
    {
        ResultCode result = RC_Success;
        IMCodec::ImageSharedPtr image = fImageManager.GetImage(handle);
        if (image != nullptr)
        {
            const bool applyExif = (display_flags & OIV_CMD_DisplayImage_Flags::DF_ApplyExifTransformation) != 0;
            if (applyExif)
                image = ApplyExifRotation(image);
            
            image = IMUtil::ImageUtil::Convert(image, IMCodec::TexelFormat::TF_I_R8_G8_B8_A8);

            if (image != nullptr)
            {
                if (fRenderer->SetImage(image) == RC_Success)
                {
                    const bool resetScrollState = (display_flags & OIV_CMD_DisplayImage_Flags::DF_ResetScrollState) != 0;
                    fActiveHandle = handle;
                    fDisplayedImage = image;
                    fScrollState.Reset(resetScrollState);
                }
                else
                {
                    result = RC_RenderError;
                }
            }
            else
            {
                result = RC_PixelFormatConversionFailed;
            }
        }
        else
        {
            result = RC_InvalidImageHandle;
        }
     
        return result;
    }

    double OIV::Zoom(double percentage, int x, int y)
    {
        fScrollState.Zoom(percentage, x, y);
        return 0.0;
    }

    int OIV::Pan(double x, double y)
    {
        fScrollState.Pan({ x, y });
        return 0.0;
    }

    int OIV::Init()
    {
        fRenderer = CreateBestRenderer();
        fRenderer->Init(fParent);
        return 0;
    }


    int OIV::SetParent(std::size_t handle)
    {
        fParent = handle;
        return 0;
    }
    int OIV::Refresh()
    {
        if (fIsRefresing == false)
        {
            fIsRefresing = true;
            HandleWindowResize();
            UpdateGpuParams();
            fRenderer->Redraw();
            fIsRefresing = false;
        }

        return 0;
    }

 /*   IMCodec::Image* OIV::GetDisplayImage()
    {
        return fDisplayedImage.get();
    }*/

    IMCodec::Image* OIV::GetImage(ImageHandle handle)
    {
        return fImageManager.GetImage(handle).get();
    }

    int OIV::SetFilterLevel(OIV_Filter_type filter_level)
    {
        if (filter_level >= FT_None && filter_level < FT_Count)
        {
            fRenderer->SetFilterLevel(filter_level);
            Refresh();
            return RC_Success;
        }

        return RC_WrongParameters;
    }

    int OIV::GetFileInformation(QryFileInformation& information)
    {

        //TODO: restore implementation and add image handle
        //if (IsImageLoaded())
        {

          /*  information.bitsPerPixel = GetOpenedImage()->GetBitsPerTexel();
            information.height = GetOpenedImage()->GetHeight();
            information.width = GetOpenedImage()->GetWidth();
            information.numMipMaps = 0;
            information.rowPitchInBytes = GetOpenedImage()->GetRowPitchInBytes();
            information.hasTransparency = 1;
            information.imageDataSize = 0;
            information.numChannels = 0;
*/
            return RC_Success;
        }
  /*      else
        {
            return 1;
        }*/
    }

    int OIV::GetTexelAtMousePos(int mouseX, int mouseY, double& texelX, double& texelY)
    {
        LLUtils::PointF64 texelPos = this->fScrollState.ClientPosToTexel({ mouseX, mouseY });
        texelX = texelPos.x;
        texelY = texelPos.y;
        return RC_Success;
    }

    int OIV::SetTexelGrid(double gridSize)
    {
        fShowGrid = gridSize > 0.0;
        Refresh();
        return RC_Success;
    }

    int OIV::GetNumTexelsInCanvas(double &x, double &y)
    {
        LLUtils::PointF64 canvasSize = fScrollState.GetNumTexelsInCanvas();
        x = canvasSize.x;
        y = canvasSize.y;
        return RC_Success;
    }

    int OIV::SetClientSize(uint16_t width, uint16_t height)
    {
        fClientWidth = width;
        fClientHeight = height;
        Refresh();
        return 0;
    }

    ResultCode OIV::AxisAlignTrasnform(const OIV_AxisAlignedRTransform transform)
    {
        if (GetDisplayImage() != nullptr)
        {
            IMCodec::ImageSharedPtr& image = fDisplayedImage;
            
            image = IMUtil::ImageUtil::Transform(static_cast<IMUtil::AxisAlignedRTransform>(transform), image);
            if (image != nullptr && fRenderer->SetImage(image) == RC_Success)
            {
                fScrollState.Reset(true);
                return RC_Success;
            }
        }
        return RC_UknownError;
    }
#pragma endregion

}
