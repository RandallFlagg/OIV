#include "PreCompiled.h"
#include "oiv.h"
#include "StringUtility.h"
namespace OIV
{
    // IPictureViewr implementation
    int OIV::LoadFile(OIVCHAR* filePath,bool onlyRegisteredExtension)
    {
        using namespace Ogre;
        std::string path = StringUtility::ToAString(filePath);

        ImageUniquePtr image = ImageUniquePtr(fImageLoader.LoadImage(path,onlyRegisteredExtension));


        //refactor out to unload
        if (fOpenedImage.get())
        {
            TextureManager::getSingleton().remove(fCurrentOpenedFile);
        }


        if (image.get())
        {
            fOpenedImage.swap(image);
            fCurrentOpenedFile = path;

            int width = fOpenedImage->GetWidth();
            int height = fOpenedImage->GetHeight();

                TexturePtr tex = TextureManager::getSingleton().createManual(
                    fCurrentOpenedFile
                    , ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME
                    , TEX_TYPE_2D
                    , width// width
                    , height // height
                    , 1      // depth
                    , 0      // num mipmaps
                    , Ogre::PF_R8G8B8A8); // pixel format


            PixelFormat format =  PF_UNKNOWN; 


            switch (fOpenedImage->GetImageType())
            {
            case  IT_BYTE_RGB:
                format = PF_R8G8B8;
                break;
            case IT_BYTE_BGRA:
                format = PF_B8G8R8A8;
                break;
            case  IT_BYTE_RGBA:
                format = PF_A8B8G8R8;
                    break;
            case IT_BYTE_BGR:
                format = PF_B8G8R8;
                break;
            case IT_BYTE_ARGB:
                format = PF_A8R8G8B8;
                break;
            case IT_BYTE_ABGR:
                format = PF_A8B8G8R8;
            case IT_BYTE_8BIT:
                format = PF_L8;
                break;
            default:
                format = PF_UNKNOWN;

            }

            if (format != PF_UNKNOWN)
            {

                PixelBox src(static_cast<uint32>(width), static_cast<uint32>(height), 1, format, const_cast<void*> (fOpenedImage->GetBuffer()));
                src.rowPitch = fOpenedImage->GetRowPitchInTexels();
                src.slicePitch = fOpenedImage->GetSlicePitchInTexels();
                Ogre::Image::Box dest(0, 0, width, height);
                HardwarePixelBufferSharedPtr buf = tex->getBuffer();
                buf->blitFromMemory(src, dest);


                fPass->getTextureUnitState(0)->setTextureName(path);
                fScrollState.Reset(true);
            }
            return true;
        }
        return false;
    }

    double OIV::Zoom(double percentage,int x, int y)
    {
        fScrollState.Zoom(percentage,x ,y);
        return 0.0;
    }

    int OIV::Pan(double x, double y)
    {
        fScrollState.Pan(Ogre::Vector2(x, y));
        return 0.0;
    }

    int OIV::Init()
    {
        InitAll();
        return 0;
    }
    

    int OIV::SetParent(HWND handle)
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
            Ogre::Root::getSingleton().renderOneFrame();
            fIsRefresing = false;
        }

        return 0;
    }

     Image* OIV::GetImage() 
    {
        return fOpenedImage.get();
    }

    int OIV::SetFilterLevel(int filter_level)
    {
        int desiredFilterLevel = filter_level;
        if (desiredFilterLevel >=0 && desiredFilterLevel <= 1)
        {
            fFilterLevel = desiredFilterLevel;
            ApplyFilter();
            Refresh();
            return RC_Success;
        }

        return RC_WrongParameters;
    }

    int OIV::GetFileInformation(QryFileInformation& information)
    {
        if (IsImageLoaded())
        {
                    
            information.bitsPerPixel = fOpenedImage->GetBitsPerTexel();
            information.height = fOpenedImage->GetHeight();
            information.width = fOpenedImage->GetWidth();
            information.numMipMaps = 0;
            information.rowPitchInBytes = fOpenedImage->GetRowPitchInBytes();
            information.hasTransparency = 1;
            information.imageDataSize = 0;
            information.numChannels = 0;

            return RC_Success;
        }
        else
        {
            return 1;
        }
    }

    int OIV::GetTexelAtMousePos(int mouseX, int mouseY, double& texelX, double& texelY)
    {
        Ogre::Vector2 texelPos = this->fScrollState.ClientPosToTexel( Ogre::Vector2(mouseX, mouseY));
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

    int OIV::GetCanvasSize(double &x, double &y)
    {
        Ogre::Vector2 canvasSize = fScrollState.GetCanvasSize();
        x = canvasSize.x;
        y = canvasSize.y;
        return RC_Success;
    }
    
}