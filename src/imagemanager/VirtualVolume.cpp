//------------------------------------------------------------------------------------------------
// Copyright (c) 2012  Alessandro Bria and Giulio Iannello (University Campus Bio-Medico of Rome).  
// All rights reserved.
//------------------------------------------------------------------------------------------------

/*******************************************************************************************************************************************************************************************
*    LICENSE NOTICE
********************************************************************************************************************************************************************************************
*    By downloading/using/running/editing/changing any portion of codes in this package you agree to this license. If you do not agree to this license, do not download/use/run/edit/change
*    this code.
********************************************************************************************************************************************************************************************
*    1. This material is free for non-profit research, but needs a special license for any commercial purpose. Please contact Alessandro Bria at a.bria@unicas.it or Giulio Iannello at 
*       g.iannello@unicampus.it for further details.
*    2. You agree to appropriately cite this work in your related studies and publications.
*    3. This material is provided by  the copyright holders (Alessandro Bria  and  Giulio Iannello),  University Campus Bio-Medico and contributors "as is" and any express or implied war-
*       ranties, including, but  not limited to,  any implied warranties  of merchantability,  non-infringement, or fitness for a particular purpose are  disclaimed. In no event shall the
*       copyright owners, University Campus Bio-Medico, or contributors be liable for any direct, indirect, incidental, special, exemplary, or  consequential  damages  (including, but not 
*       limited to, procurement of substitute goods or services; loss of use, data, or profits;reasonable royalties; or business interruption) however caused  and on any theory of liabil-
*       ity, whether in contract, strict liability, or tort  (including negligence or otherwise) arising in any way out of the use of this software,  even if advised of the possibility of
*       such damage.
*    4. Neither the name of University  Campus Bio-Medico of Rome, nor Alessandro Bria and Giulio Iannello, may be used to endorse or  promote products  derived from this software without
*       specific prior written permission.
********************************************************************************************************************************************************************************************/

/******************
*    CHANGELOG    *
*******************
* 2020-01-28. Giulio.     @ADDED support for multi-slice and multi-cycle formats
* 2018-06-30. Giulio.     @ADDED initialization of 'depth_conv_algo' (ID of algorithm to be used to convert from arbitrary depth to 8 bits)
* 2017-08-30. Giulio.     @FIXED introduced sint64 variables to avoid overflow in indices on slices larger than 4 GBs
* 2017-04-13. Giulio.     @ADDED the case 2D slices multi-channel 16 bit in 'saveImage_from_UINT8'
* 2016-05-11. Giluio.     @FIXED a bug in 'halveSample2D_UINT8' ('img16' in plage of 'img' at line 1085)
* 2016-04-15. Giluio.     @FIXED in 'saveImage_from_UINT8' offset must not be doubled when color depth is 16 bits
* 2015-12-10. Giluio.     @FIXED added several volume creation alternatives in "instance" methods to include new formats 
* 2015-04-15. Alessandro. @ADDED 'instance_format' method with inputs = {path, format}.
* 2015-04-15. Alessandro. @ADDED definition for default constructor.
* 2015-04-14. Alessandro. @FIXED detection of volume format from .iim.format file
* 2015-03-21. Giulio.     @FIXED bug in saveImage when tiles have to be saved (the management of offsets over the buffer was wrong)
* 2015-03-03. Giulio.     @FIXED a 2D plugin has to be used in saveImage_from_UINT8 
* 2015-02-15. Giulio.     @CHANGED revised all calls to Tiff3DMngr routines passing always width and height in this order
* 2015-02-14. Giulio.     @CHANGED method saveImage now converts from real to uint8 and calls the new interface of the plugin
* 2015-02-13. Giulio.     @CHANGED method saveImage_from_UINT8_to_Tiff3D now call a 3D pluging to save a slice (only when do_open is true)
* 2015-02-12. Giulio.     @FIXED bug concerning the number of channels in the saved image in method 'saveImage_from_UINT8_to_Tiff3D'
* 2015-02-06. Giulio.     @ADDED added optimizations to reduce opend/close in append operations (only in saveImage_from_UINT8_to_Tiff3D)
* 2015-02-06. Alessandro. @ADDED volume format metadata file for fast opening of a "directory format"
* 2014-11-22. Giulio.     @CHANGED code using OpenCV has been commente. It can be found searching comments containing 'Giulio_CV'
* 2014-11-10. Giulio.     @CHANGED allowed saving 2dseries with a depth of 16 bit (generateTiles)
*/

#include <fstream>
#include "VirtualVolume.h"
#include "SimpleVolume.h"
#include "SimpleVolumeRaw.h"
#include "RawVolume.h"
#include "TiledVolume.h"
#include "TiledMCVolume.h"
#include "StackedVolume.h"
#include "UnstitchedVolume.h"
#include "MultiSliceVolume.h"
#include "MultiCycleVolume.h"
#include "BDVVolume.h"
#include "RawFmtMngr.h"
#include "Tiff3DMngr.h"
#include "TimeSeries.h"
#include <typeinfo>

// Giulio_CV #include <cxcore.h>
// Giulio_CV #include <highgui.h>
#include "IOPluginAPI.h" // 2014-11-26. Giulio. 


#include <stdio.h>


using namespace iim;

// 2015-04-15. Alessandro. @ADDED definition for default constructor.
VirtualVolume::VirtualVolume(void)
{
    /**/iim::debug(iim::LEV3, 0, __iim__current__function__);
    
    root_dir = 0;
    VXL_V = VXL_H = VXL_D = ORG_V = ORG_H = ORG_D = 0.0f;
    DIM_V = DIM_H = DIM_D = DIM_C = 0;
    BYTESxCHAN = 0;
    active = 0;
    n_active = 0;
    t0 = t1 = 0;
    DIM_T = 1;
    depth_conv_algo = DEPTH_CONVERSION_DEFAULT;
}


void VirtualVolume::setDEPTH_CONV_ALGO(int algoID) {
	if ( ((BYTESxCHAN == 1) && ((algoID & MASK_REMAP_ALGORITHM) || algoID == REMAP_NULL)) || ((BYTESxCHAN > 1) && (algoID & MASK_CONVERSION_ALGORITHM)) )
		// either it is an 8 bits image and algorithm is a remapping or a null transformation, or it is a 16 or more bits images and the algorithm is a conversion to 8 bits
		depth_conv_algo = algoID; 
	else
		throw iom::exception(iom::strprintf("invalid remap or conversion algorithm (%d) for %d bits per sample ", algoID, BYTESxCHAN * 8), __iom__current__function__);
}


/*************************************************************************************************************
* Save image method. <> parameters are mandatory, while [] are optional.
* <img_path>                : absolute path of image to be saved. It DOES NOT include its extension, which is
*                             provided by the [img_format] parameter.
* <raw_img>                 : image to be saved. Raw data is in [0,1] and it is stored row-wise in a 1D array.
* <raw_img_height/width>    : dimensions of raw_img.
* [start/end_height/width]  : optional ROI (region of interest) to be set on the given image.
* [img_format]              : image format extension to be used (e.g. "tif", "png", etc.)
* [img_depth]               : image bitdepth to be used (8 or 16)
**************************************************************************************************************/
void VirtualVolume::saveImage(std::string img_path, real32* raw_img, int raw_img_height, int  raw_img_width,
							 int start_height, int end_height, int start_width, int end_width, 
                             const char* img_format, int img_depth)
{
    /**/iim::debug(iim::LEV3, strprintf("img_path=%s, raw_img_height=%d, raw_img_width=%d, start_height=%d, end_height=%d, start_width=%d, end_width=%d",
                                        img_path.c_str(), raw_img_height, raw_img_width, start_height, end_height, start_width, end_width).c_str(), __iim__current__function__);

	//throw IOException("in VirtualVolume::saveImage(...): disabled to remove dependence from openCV"); // Giulio_CV

	// Giulio_CV uint8  *row_data_8bit;
	// Giulio_CV uint16 *row_data_16bit;
	// Giulio_CV uint32 img_data_step;
	// Giulio_CV float scale_factor_16b, scale_factor_8b;
	sint64 img_height, img_width;
	// Giulio_CV int i,j;
	char img_filepath[5000];

	//setting some default parameters and image dimensions
	end_height = (end_height == -1 ? raw_img_height - 1 : end_height);
	end_width  = (end_width  == -1 ? raw_img_width  - 1 : end_width );
	img_height = end_height - start_height + 1;
	img_width  = end_width  - start_width  + 1;

	//checking parameters correctness
	if(!(start_height>=0 && end_height>start_height && end_height<raw_img_height && start_width>=0 && end_width>start_width && end_width<raw_img_width))
		throw iom::exception(iom::strprintf("invalid ROI [%d,%d](X) x [%d,%d](Y) on image %d(X) x %d(Y)", start_width, end_width, start_height, end_height, raw_img_width, raw_img_height), __iom__current__function__);
	//{
	//	char err_msg[STATIC_STRINGS_SIZE];
	//	sprintf(err_msg,"in saveImage(..., raw_img_height=%d, raw_img_width=%d, start_height=%d, end_height%d, start_width=%d, end_width=%d): invalid image portion\n",
	//		raw_img_height, raw_img_width, start_height, end_height, start_width, end_width);
 //       throw IOException(err_msg);
	//}
	if(img_depth != 8 && img_depth != 16)
		throw iom::exception(iom::strprintf("unsupported bitdepth %d\n", img_depth), __iom__current__function__);
	//{
	//	char err_msg[STATIC_STRINGS_SIZE];		
	//	sprintf(err_msg,"in saveImage(..., img_depth=%d, ...): unsupported bit depth\n",img_depth);
 //       throw IOException(err_msg);
	//}

	unsigned char *buffer = new unsigned char[img_height * img_width * (img_depth/8)];

	if(img_depth == 8)
	{
		for(int i = 0, ii = start_height; i <img_height; i++, ii++)
		{
			uint8* img_data = buffer + i*img_width;
			for(int j = 0, jj = start_width; j < img_width; j++, jj++)
				img_data[j] = static_cast<uint8>(raw_img[ii*((sint64)raw_img_width)+jj] * 255.0f + 0.5f);
		}
	}
	else // img_depth == 16
	{
		for(int i = 0, ii = start_height; i <img_height; i++, ii++)
		{
			uint16* img_data = ((uint16 *)buffer) + i*img_width; // the cast to uint16* guarantees the right offset
			for(int j = 0, jj = start_width; j < img_width; j++, jj++)
				img_data[j] = static_cast<uint16>(raw_img[ii*((sint64)raw_img_width)+jj] * 65535.0f + 0.5f);
		}
	}

	//generating complete path for image to be saved
	sprintf(img_filepath, "%s.%s", img_path.c_str(), img_format);

	// 2014-11-26. Giulio. @ADDED the output plugin must be explicitly set by the caller 
	try
	{
		//iomanager::IOPluginFactory::getPlugin2D(iomanager::IMOUT_PLUGIN)->writeData(
		//	img_filepath, raw_img, img_height, img_width, 1, start_height, end_height, start_width, end_width, img_depth);
		iomanager::IOPluginFactory::getPlugin2D(iomanager::IMOUT_PLUGIN)->writeData(
			img_filepath, buffer, (int)img_height, (int)img_width, img_depth/8, 1, 0, (int)img_height, 0, (int)img_width); // ROI limits specify right-open intervals  
	}
	catch (iom::exception & ex)
	{
		if ( strstr(ex.what(),"it is not a 2D I/O plugin") ) // this method has be called to save the middle slice for test purposes, even though output plugin is not a 2D plugin
			iomanager::IOPluginFactory::getPlugin2D("tiff2D")->writeData(
				img_filepath, buffer, (int)img_height, (int)img_width, img_depth/8, 1, 0, (int)img_height, 0, (int)img_width); // ROI limits specify right-open intervals
		else
			throw iom::exception(iom::strprintf(ex.what()), __iom__current__function__);
	}

	delete []buffer;
}

/*************************************************************************************************************
* Save image method from uint8 raw data. <> parameters are mandatory, while [] are optional.
* <img_path>                : absolute path of image to be saved. It DOES NOT include its extension, which is
*                             provided by the [img_format] parameter.
* <raw_img_height/width>    : dimensions of raw_img.
* <raw_ch1>                 : raw data of the first channel with values in [0,255].
*                             For grayscale images this is the pointer to the raw image data.
*                             For colour images this is the pointer to the raw image data of the RED channel.
* <raw_ch2>                 : raw data of the second channel with values in [0,255].
*                             For grayscale images this should be a null pointer (as it is by default).
*                             For colour images this is the pointer to the raw image data of the GREEN channel.
* <raw_ch3>                 : raw data of the third channel with values in [0,255].
*                             For grayscale images this should be a null pointer (as it is by default).
*                             For colour images this is the pointer to the raw image data of the BLUE channel.
* [start/end_height/width]  : optional ROI (region of interest) to be set on the given image.
* [img_format]              : image format extension to be used (e.g. "tif", "png", etc.)
* [img_depth]               : image bitdepth to be used (8 or 16)
**************************************************************************************************************/
void VirtualVolume::saveImage_from_UINT8 (std::string img_path, uint8* raw_ch1, uint8* raw_ch2, uint8* raw_ch3, 
                           int raw_img_height, int raw_img_width, int start_height, int end_height, int start_width,
                           int end_width, const char* img_format, int img_depth )
{
    /**/iim::debug(iim::LEV3, strprintf("img_path=%s, raw_img_height=%d, raw_img_width=%d, start_height=%d, end_height=%d, start_width=%d, end_width=%d, img_format=%s, img_depth=%d",
                                        img_path.c_str(), raw_img_height, raw_img_width, start_height, end_height, start_width, end_width, img_format, img_depth).c_str(), __iim__current__function__);

	// throw IOException("in VirtualVolume::saveImage_from_UINT8(...): disabled to remove dependence from openCV"); // Giulio_CV

	if ( strcmp(img_format,"tif") != 0 && strcmp(img_format,"raw") != 0) 
		throw iom::exception(iom::strprintf("unsupported file format %s",img_format), __iom__current__function__);

    //checking for non implemented features
	//if( img_depth != 8 ) {
	//	char err_msg[STATIC_STRINGS_SIZE];
	//	sprintf(err_msg,"SimpleVolume::loadSubvolume_to_UINT8: invalid number of bits per channel (%d)",img_depth); 
	//		throw IOException(err_msg);
	//}

    //LOCAL VARIABLES
    char buffer[STATIC_STRINGS_SIZE];
    // Giulio_CV IplImage* img = 0;
	uint8 *img = (uint8 *) 0;
    sint64 img_height, img_width;
    int nchannels = 0;

    //detecting the number of channels (WARNING: the number of channels is an attribute of the volume
    nchannels = static_cast<int>(raw_ch1!=0) + static_cast<int>(raw_ch2!=0) + static_cast<int>(raw_ch3!=0);

    //setting some default parameters and image dimensions
    end_height = (end_height == -1 ? raw_img_height - 1 : end_height);
    end_width  = (end_width  == -1 ? raw_img_width  - 1 : end_width );
    img_height = end_height - start_height + 1;
    img_width  = end_width  - start_width  + 1;

    //checking parameters correctness
    if(!(start_height>=0 && end_height>start_height && end_height<raw_img_height && start_width>=0 && end_width>start_width && end_width<raw_img_width))
    {
        sprintf(buffer,"in saveImage_from_UINT8(..., raw_img_height=%d, raw_img_width=%d, start_height=%d, end_height%d, start_width=%d, end_width=%d): invalid image portion\n",
                raw_img_height, raw_img_width, start_height, end_height, start_width, end_width);
        throw IOException(buffer);
    }
	// nchannels may be also 2
    //if(nchannels != 1 && nchannels != 3)
    if(nchannels > 3)
    {
        sprintf(buffer,"in saveImage_from_UINT8(): unsupported number of channels (= %d)\n",nchannels);
        throw IOException(buffer);
    }
    if(img_depth != 8 && img_depth != 16 && nchannels == 1)
    {
        sprintf(buffer,"in saveImage_from_UINT8(..., img_depth=%d, ...): unsupported bit depth for greyscale images\n",img_depth);
        throw IOException(buffer);
    }
    //if(img_depth != 8 && nchannels == 3) // nchannels may be also 2
    if(img_depth != 8 && img_depth != 16 && nchannels > 1)
    {
        sprintf(buffer,"in saveImage_from_UINT8(..., img_depth=%d, ...): unsupported bit depth for multi-channels images\n",img_depth);
        throw IOException(buffer);
    }

    //generating complete path for image to be saved
    sprintf(buffer, "%s.%s", img_path.c_str(), img_format);


	if ( strcmp(img_format,"tif") == 0 ) { // these code should be executed only if the output format is TIFF

		// 2016-04-09 Giulio. @ADDED to debug error on wrong 2D plugin
		if ( iom::IMOUT_PLUGIN.compare("empty") == 0 || iom::IMOUT_PLUGIN.compare("tiff3D") == 0 ) {
			throw iom::exception(iom::strprintf("wrong input plugin (%s).",iom::IMOUT_PLUGIN.c_str()), __iom__current__function__);
		}
		if ( nchannels >1 && !(iom::IOPluginFactory::getPlugin2D(iom::IMOUT_PLUGIN)->isChansInterleaved()) ) {
			throw iom::exception("in saveImage_from_UINT8(...): the plugin do not store channels in interleaved mode: more than one channel not supported yet.");
		}

		//converting raw data into tif image data
		if(nchannels == 3)
		{
			img = new uint8[img_width * img_height * nchannels * (img_depth/8)]; // Giulio_CV cvCreateImage(cvSize(img_width, img_height), IPL_DEPTH_8U, 3);
			sint64 img_data_step = img_width * nchannels;
			if(img_depth == 8)
			{
				for(int i = 0; i <img_height; i++)
				{
					uint8* row_data_8bit = img + i*img_data_step;
					for(int j1 = 0, j2 = start_width; j1 < img_width*3; j1+=3, j2++)
					{
						row_data_8bit[j1  ] = raw_ch1[(i+start_height)*((sint64)raw_img_width) + j2];
						row_data_8bit[j1+1] = raw_ch2[(i+start_height)*((sint64)raw_img_width) + j2];
						row_data_8bit[j1+2] = raw_ch3[(i+start_height)*((sint64)raw_img_width) + j2];
					}
				}
			}
			else // img->depth == 16
			{
				for(int i = 0; i <img_height; i++)
				{
					uint16* row_data_16bit = ((uint16*)img) + i*img_data_step;
					for(int j1 = 0, j2 = start_width; j1 < img_width*3; j1+=3, j2++) {
						// row_data_16bit[j] = (uint16) (raw_ch1[(i+start_height)*raw_img_width+j+start_width] * scale_factor_16b); // conversion is not supported yet
						row_data_16bit[j1  ] = ((uint16 *) raw_ch1)[(i+start_height)*((sint64)raw_img_width)+j2];
						row_data_16bit[j1+1] = ((uint16 *) raw_ch2)[(i+start_height)*((sint64)raw_img_width)+j2];
						row_data_16bit[j1+2] = ((uint16 *) raw_ch3)[(i+start_height)*((sint64)raw_img_width)+j2];
					}
				}
			}
		}
		else if(nchannels == 2)
		{
			// stores in any case as a 3 channels image (RGB)
			img = new uint8[img_width * img_height * (nchannels+1) * (img_depth/8)]; // Giulio_CV cvCreateImage(cvSize(img_width, img_height), IPL_DEPTH_8U, 3);
			sint64 img_data_step = img_width * (nchannels+1); 
			if(img_depth == 8)
			{
				for(int i = 0; i <img_height; i++)
				{
					uint8* row_data_8bit = img + i*img_data_step;
					for(int j1 = 0, j2 = start_width; j1 < img_width*3; j1+=3, j2++)
					{
						row_data_8bit[j1  ] = raw_ch1[(i+start_height)*((sint64)raw_img_width) + j2];
						row_data_8bit[j1+1] = raw_ch2[(i+start_height)*((sint64)raw_img_width) + j2];
						row_data_8bit[j1+2] = 0;
					}
				}
			}
			else // img->depth == 16
			{
				for(int i = 0; i <img_height; i++)
				{
					uint16* row_data_16bit = ((uint16*)img) + i*img_data_step;
					for(int j1 = 0, j2 = start_width; j1 < img_width*3; j1+=3, j2++) {
						// row_data_16bit[j] = (uint16) (raw_ch1[(i+start_height)*raw_img_width+j+start_width] * scale_factor_16b); // conversion is not supported yet
						row_data_16bit[j1  ] = ((uint16 *) raw_ch1)[(i+start_height)*((sint64)raw_img_width)+j2];
						row_data_16bit[j1+1] = ((uint16 *) raw_ch2)[(i+start_height)*((sint64)raw_img_width)+j2];
						row_data_16bit[j1+2] = 0;
					}
				}
			}
		}
		else if(nchannels == 1) // source and destination depths are guarenteed to be the same
		{
			img = new uint8[img_width * img_height * nchannels * (img_depth/8)]; // Giulio_CV cvCreateImage(cvSize(img_width, img_height), (img_depth == 8 ? IPL_DEPTH_8U : IPL_DEPTH_16U), 1);
			sint64 img_data_step = img_width * nchannels;
			//float scale_factor_16b = 65535.0F/255; // conversion is not supported yet
			if(img_depth == 8)
			{
				for(int i = 0; i <img_height; i++)
				{
					uint8* row_data_8bit = img + i*img_data_step;
					for(int j = 0; j < img_width; j++)
						row_data_8bit[j] = raw_ch1[(i+start_height)*((sint64)raw_img_width)+j+start_width];
				}
			}
			else // img->depth == 16
			{
				//int img_data_step = img_width * nchannels * 2;
				// 2016-04-15. Giulio. @FIXED offset is applied to uint16 pointers: is must not be doubled
				for(int i = 0; i <img_height; i++)
				{
					uint16* row_data_16bit = ((uint16*)img) + i*img_data_step;
					for(int j = 0; j < img_width; j++)
						// row_data_16bit[j] = (uint16) (raw_ch1[(i+start_height)*((sint64)raw_img_width)+j+start_width] * scale_factor_16b); // conversion is not supported yet
						row_data_16bit[j] = ((uint16 *) raw_ch1)[(i+start_height)*((sint64)raw_img_width)+j+start_width] /* 2014-11-10. Giulio.    removed: [* scale_factor_16b )] */;
				}
			}
		}

		char *err_tiff_fmt;

		// creates the file (2D image: depth is 1)
		if ( (err_tiff_fmt = initTiff3DFile((char *)img_path.c_str(),(int)img_width,(int)img_height,1,(nchannels>1 ? 3 : 1),img_depth/8)) != 0 ) {
			throw iom::exception(iom::strprintf("unable to create tiff file (%s)",err_tiff_fmt), __iom__current__function__);
		}

		if ( (err_tiff_fmt = appendSlice2Tiff3DFile(buffer,0,(unsigned char *)img,(int)img_width,(int)img_height)) != 0 ) {
			throw iom::exception(iom::strprintf("error in saving 2D image (%lld x %lld) in file %s (appendSlice2Tiff3DFile: %s)",img_width,img_height,buffer,err_tiff_fmt), __iom__current__function__);
		}

		if ( img )
			delete []img;
	}
	else { // raw format

		char *err_rawfmt;
		V3DLONG sz[4];
		sz[0] = img_width;
		sz[1] = img_height;
		sz[2] = 1;
		sz[3] = nchannels;

		img = raw_ch1 + (start_height*raw_img_width + start_width); // assume that there is just one channel: in this case do not copy
		// chack that the channel buffers are contiguous
		if ( nchannels > 1 ) { // there are al least two channels
// 			if ( (raw_ch2 - raw_ch1) != (sz[0] * sz[1] * sz[2] * (img_depth/8)) )
// 				throw iom::exception(iom::strprintf("error in saving 2D image (%lld x %lld) in file %s: channel buffers not contiguous",img_height,img_width,buffer), __iom__current__function__);
			sint64 chanSize = img_width * img_height * (img_depth/8);
			img = new uint8[chanSize * nchannels]; 
			memcpy(img,(raw_ch1 + (start_height*((sint64)raw_img_width) + start_width)),chanSize);
			memcpy(img + chanSize,(raw_ch2 + (start_height*((sint64)raw_img_width) + start_width)),chanSize);
			if ( nchannels > 2 ) { // there are three channels
// 				if ( (raw_ch3 - raw_ch2) != (sz[0] * sz[1] * sz[2] * (img_depth/8)) )
// 					throw iom::exception(iom::strprintf("error in saving 2D image (%lld x %lld) in file %s: channel buffers not contiguous",img_height,img_width,buffer), __iom__current__function__);
				memcpy(img + 2*chanSize,(raw_ch3 + (start_height*((sint64)raw_img_width) + start_width)),chanSize);
			}
		}

		if ( (err_rawfmt = saveWholeStack2Raw(buffer,(unsigned char *)img,sz,(img_depth/8))) != 0 ) {
			throw iom::exception(iom::strprintf("error in saving 2D image (%lld x %lld) in file %s (writeSlice2RawFile: %s)",img_height,img_width,buffer,err_rawfmt), __iom__current__function__);
		}

		if ( nchannels > 1 && img )
			delete []img;
	} // end if ( strcmp(img_format,"tif") != 0 )

	/* Giulio_CV

	}
    catch(std::exception ex)
    {
        char err_msg[STATIC_STRINGS_SIZE];
        sprintf(err_msg,"in saveImage_from_UINT8(...): unable to save image at \"%s\". Unsupported format or wrong path.\n",buffer);
        throw IOException(err_msg);
    }

    releasing memory
    cvReleaseImage(&img);

	*/
}



/*************************************************************************************************************
* Save image method to Vaa3D raw format. <> parameters are mandatory, while [] are optional.
* <img_path>                : absolute path of image to be saved. It DOES NOT include its extension, which is
*                             provided by the [img_format] parameter.
* <raw_img>                 : image to be saved. Raw data is in [0,1] and it is stored row-wise in a 1D array.
* <raw_img_height/width>    : dimensions of raw_img.
* [start/end_height/width]  : optional ROI (region of interest) to be set on the given image.
* [img_format]              : image format extension to be used (e.g. "tif", "png", etc.)
* [img_depth]               : image bitdepth to be used (8 or 16)
**************************************************************************************************************/
void VirtualVolume::saveImage_to_Vaa3DRaw(int slice, std::string img_path, real32* raw_img, int raw_img_height, int  raw_img_width,
						 int start_height, int end_height, int start_width, int end_width, 
						 const char* img_format, int img_depth
                         )
{
    /**/iim::debug(iim::LEV3, strprintf("img_path=%s, raw_img_height=%d, raw_img_width=%d, start_height=%d, end_height=%d, start_width=%d, end_width=%d", img_path.c_str(), raw_img_height, raw_img_width, start_height, end_height, start_width, end_width).c_str(), __iim__current__function__);

    //checking for non implemented features
	if ( strcmp(img_format,"Vaa3DRaw")!=0 && strcmp(img_format,"Tiff3D")!=0 ) { // WARNING: there is not a well defined convention yet for specify image format
		char msg[STATIC_STRINGS_SIZE];
		sprintf(msg,"in VirtualVolume::saveImage_to_Vaa3DRaw: format \"%s\" not implemented yet",img_format);
		throw IOException(msg);
	}

	uint8  *row_data_8bit;
	uint16 *row_data_16bit;
	//uint32 img_data_step;
	float scale_factor_16b, scale_factor_8b;
	sint64 img_height, img_width;
	int i, j;
	sint64 k; // 2017-08-30. Giulio. in case a slice is larger than 4 GB
	char img_filepath[5000];

	// WARNING: currently supported only 8/16 bits depth by VirtualVolume::saveImage_from_UINT8_to_Vaa3DRaw
	if(img_depth != 8 && img_depth != 16)
	{
		char err_msg[STATIC_STRINGS_SIZE];		
		sprintf(err_msg,"in saveImage_to_Vaa3DRaw(..., img_depth=%d, ...): unsupported bit depth\n",img_depth);
        throw IOException(err_msg);
	}

	//setting some default parameters and image dimensions
	end_height = (end_height == -1 ? raw_img_height - 1 : end_height);
	end_width  = (end_width  == -1 ? raw_img_width  - 1 : end_width );
	img_height = end_height - start_height + 1;
	img_width  = end_width  - start_width  + 1;

	//checking parameters correctness
	if(!(start_height>=0 && end_height>start_height && end_height<raw_img_height && start_width>=0 && end_width>start_width && end_width<raw_img_width))
	{
		char err_msg[STATIC_STRINGS_SIZE];
		sprintf(err_msg,"in saveImage_to_Vaa3DRaw(..., raw_img_height=%d, raw_img_width=%d, start_height=%d, end_height%d, start_width=%d, end_width=%d): invalid image portion\n",
			raw_img_height, raw_img_width, start_height, end_height, start_width, end_width);
        throw IOException(err_msg);
	}

	if ( strcmp(img_format,"Vaa3DRaw")==0 ) {
		//generating complete path for image to be saved
		sprintf(img_filepath, "%s.%s", img_path.c_str(), VAA3D_SUFFIX);
	}
	else if ( strcmp(img_format,"Tiff3D")==0 ) {
		sprintf(img_filepath, "%s.%s", img_path.c_str(), "tif");
	}

	//converting raw data in image data
	scale_factor_16b = 65535.0F;
	scale_factor_8b  = 255.0F;
	if(img_depth == 8)
	{
		row_data_8bit = new uint8[img_height * img_width];
		for(i = 0, k = 0; i <img_height; i++)
		{
			for(j = 0; j < img_width; j++, k++)
				row_data_8bit[k] = (uint8) (raw_img[(i+start_height)*((sint64)raw_img_width)+j+start_width] * scale_factor_8b);
		}
	}
	else // img_depth = 16 (because of the check above)
	{
		row_data_16bit = new uint16[img_height * img_width];
		for(i = 0, k = 0; i <img_height; i++)
		{
			for(j = 0; j < img_width; j++, k++)
				row_data_16bit[k] = (uint16) (raw_img[(i+start_height)*((sint64)raw_img_width)+j+start_width] * scale_factor_16b);
		}
		row_data_8bit = (uint8 *) row_data_16bit;
	}

	if ( strcmp(img_format,"Vaa3DRaw")==0 ) {
		VirtualVolume::saveImage_from_UINT8_to_Vaa3DRaw (slice,img_path, &row_data_8bit, 1, 0, 
								(int)img_height, (int)img_width, 0, -1, 0, -1, img_format, img_depth); // the ROI coincides with the whole buffer (row_data8bit) 
	}
	else if ( strcmp(img_format,"Tiff3D")==0 ) {
		VirtualVolume::saveImage_from_UINT8_to_Tiff3D (slice,img_path, &row_data_8bit, 1, 0, 
								(int)img_height, (int)img_width, 0, -1, 0, -1, img_format, img_depth); // the ROI coincides with the whole buffer (row_data8bit) 
	}

	delete row_data_8bit;
}


/*************************************************************************************************************
* Save image method from uint8 raw data to Vaa3D raw format. <> parameters are mandatory, while [] are optional.
* <img_path>                : absolute path of image to be saved. It DOES NOT include its extension, which is
*                             provided by the [img_format] parameter.
* <raw_ch>                  : array of pointers to raw data of the channels with values in [0,255].
*                             For grayscale images raw_ch[0] is the pointer to the raw image data.
*                             For colour images raw_ch[0] is the pointer to the raw image data of the RED channel.
*                             WARNING: raw_ch elements should not be overwritten
* <n_chans>                 : number of channels (length of raw_ch).
* <offset>                  : offset to be added to raw_ch[i] to get actual data
* <raw_img_height/width>    : dimensions of raw_img.        
* [start/end_height/width]  : optional ROI (region of interest) to be set on the given image.
* [img_format]              : image format extension to be used (e.g. "tif", "png", etc.)
* [img_depth]               : image bitdepth to be used (8 or 16)
**************************************************************************************************************/
void VirtualVolume::saveImage_from_UINT8_to_Vaa3DRaw (int slice, std::string img_path, uint8** raw_ch, int n_chans, sint64 offset, 
                       int raw_img_height, int raw_img_width, int start_height, int end_height, int start_width,
                       int end_width, const char* img_format, int img_depth )
{
    /**/iim::debug(iim::LEV3, strprintf("img_path=%s, raw_img_height=%d, raw_img_width=%d, start_height=%d, end_height=%d, start_width=%d, end_width=%d", img_path.c_str(), raw_img_height, raw_img_width, start_height, end_height, start_width, end_width).c_str(), __iim__current__function__);

    //LOCAL VARIABLES
    char buffer[STATIC_STRINGS_SIZE];
    sint64 img_height, img_width;
	sint64 img_width_b; // image width in bytes
	int img_bytes_per_chan;

	//add offset to raw_ch
	//for each channel adds to raw_ch the offset of current slice from the beginning of buffer 
	uint8** raw_ch_temp = new uint8 *[n_chans];
	for (int i=0; i<n_chans; i++)
		raw_ch_temp[i] = raw_ch[i] + offset;

    //setting some default parameters and image dimensions
    end_height = (end_height == -1 ? raw_img_height - 1 : end_height);
    end_width  = (end_width  == -1 ? raw_img_width  - 1 : end_width );
    img_height = end_height - start_height + 1;
    img_width  = end_width  - start_width  + 1;

    //checking parameters correctness
    if(!(start_height>=0 && end_height>start_height && end_height<raw_img_height && start_width>=0 && end_width>start_width && end_width<raw_img_width))
    {
        sprintf(buffer,"in VirtualVolume::saveImage_from_UINT8_to_Vaa3DRaw(..., raw_img_height=%d, raw_img_width=%d, start_height=%d, end_height%d, start_width=%d, end_width=%d): invalid image portion\n",
                raw_img_height, raw_img_width, start_height, end_height, start_width, end_width);
        throw IOException(buffer);
    }

 //if(img_depth != 8 && img_depth != 16 && n_chans == 1)
 //   {
 //       sprintf(buffer,"in saveImage_from_UINT8(..., img_depth=%d, ...): unsupported bit depth for greyscale images\n",img_depth);
 //       throw iim::IOException(buffer);
 //   }
 //   if(img_depth != 8 && n_chans > 1)
 //   {
 //       sprintf(buffer,"in saveImage_from_UINT8(..., img_depth=%d, ...): unsupported bit depth for multi-channel images\n",img_depth);
 //       throw iim::IOException(buffer);
 //   }

	if(img_depth != 8 && img_depth != 16 && n_chans == 1)
	{
		sprintf(buffer,"in VirtualVolume::saveImage_from_UINT8_to_Vaa3DRaw(..., img_depth=%d, ...): unsupported bit depth\n",img_depth);
        throw IOException(buffer);
	}
	img_bytes_per_chan = (img_depth == 8) ? 1 : 2;
	// all width parameters have to be multiplied by the number of bytes per channel
	img_width_b    = img_width * img_bytes_per_chan; 
	raw_img_width *= img_bytes_per_chan;
	start_width   *= img_bytes_per_chan;

	uint8 *imageData = new uint8[img_height * img_width_b * n_chans];
	for ( int c=0; c<n_chans; c++ ) {
		for(sint64 i=0; i<img_height; i++)
		{
			uint8* row_data_8bit = imageData + c*img_height*img_width_b + i*img_width_b;
			for(sint64 j=0; j<img_width_b; j++)
				row_data_8bit[j] = raw_ch_temp[c][(i+start_height)*((sint64)raw_img_width) + (j+start_width)];
            }
	}

    //generating complete path for image to be saved
    sprintf(buffer, "%s.%s", img_path.c_str(), VAA3D_SUFFIX);

	char *err_rawfmt;
	if ( (err_rawfmt = writeSlice2RawFile (buffer,slice,(unsigned char *)imageData,(int)img_height,(int)img_width)) != 0 ) {
		char err_msg[STATIC_STRINGS_SIZE];
		sprintf(err_msg,"VirtualVolume::saveImage_from_UINT8_to_Vaa3DRaw: error in saving slice %d (%lld x %lld) in file %s (writeSlice2RawFile: %s)", slice,img_height,img_width,buffer,err_rawfmt);
        throw IOException(err_msg);
	}

	delete[] imageData;
}

/*************************************************************************************************************
* Save image method from uint8 raw data to Tiff 3D (multiPAGE) format. <> parameters are mandatory, while [] are optional.
* <img_path>                : absolute path of image to be saved. It DOES NOT include its extension, which is
*                             provided by the [img_format] parameter.
* <raw_ch>                  : array of pointers to raw data of the channels with values in [0,255].
*                             For grayscale images raw_ch[0] is the pointer to the raw image data.
*                             For colour images raw_ch[0] is the pointer to the raw image data of the RED channel.
*                             WARNING: raw_ch elements should not be overwritten
* <n_chans>                 : number of channels (length of raw_ch).
* <offset>                  : offset to be added to raw_ch[i] to get actual data
* <raw_img_height/width>    : dimensions of raw_img.        
* [start/end_height/width]  : optional ROI (region of interest) to be set on the given image.
* [img_format]              : image format extension to be used (e.g. "tif", "png", etc.)
* [img_depth]               : image bitdepth to be used (8 or 16)
**************************************************************************************************************/
void VirtualVolume::saveImage_from_UINT8_to_Tiff3D (int slice, std::string img_path, uint8** raw_ch, int n_chans, sint64 offset, 
                       int raw_img_height, int raw_img_width, int start_height, int end_height, int start_width,
                       int end_width, const char* img_format, int img_depth, void *fhandle, int n_pages, bool do_open )
{
    /**/iim::debug(iim::LEV3, strprintf("img_path=%s, raw_img_height=%d, raw_img_width=%d, start_height=%d, end_height=%d, start_width=%d, end_width=%d", img_path.c_str(), raw_img_height, raw_img_width, start_height, end_height, start_width, end_width).c_str(), __iim__current__function__);

    //LOCAL VARIABLES
    char buffer[STATIC_STRINGS_SIZE];
    sint64 img_height, img_width;
	sint64 img_width_b; // image width in bytes
	int img_bytes_per_chan;
	int temp_n_chans = n_chans; // number of channels of the saved image: initialize with the number of channels of the source volume

	//add offset to raw_ch
	//for each channel adds to raw_ch the offset of current slice from the beginning of buffer 
	uint8** raw_ch_temp = new uint8 *[n_chans];
	for (int i=0; i<n_chans; i++)
		raw_ch_temp[i] = raw_ch[i] + offset;

    //setting some default parameters and image dimensions
    end_height = (end_height == -1 ? raw_img_height - 1 : end_height);
    end_width  = (end_width  == -1 ? raw_img_width  - 1 : end_width );
    img_height = end_height - start_height + 1;
    img_width  = end_width  - start_width  + 1;

    //checking parameters correctness
    if(!(start_height>=0 && end_height>start_height && end_height<raw_img_height && start_width>=0 && end_width>start_width && end_width<raw_img_width))
    {
        sprintf(buffer,"in VirtualVolume::saveImage_from_UINT8_to_Tiff3D(..., raw_img_height=%d, raw_img_width=%d, start_height=%d, end_height%d, start_width=%d, end_width=%d): invalid image portion\n",
                raw_img_height, raw_img_width, start_height, end_height, start_width, end_width);
        throw IOException(buffer);
    }

	if(img_depth != 8 && img_depth != 16 && n_chans == 1)
	{
		sprintf(buffer,"in VirtualVolume::saveImage_from_UINT8_to_Tiff3D(..., img_depth=%d, ...): unsupported bit depth\n",img_depth);
        throw IOException(buffer);
	}

	if ( n_chans >1 && !(iom::IOPluginFactory::getPlugin3D(iom::IMOUT_PLUGIN)->isChansInterleaved()) ) {
		throw iom::exception("in VirtualVolume::saveImage_from_UINT8_to_Tiff3D: the 3D plugin do not store channels in interleaved mode: more than one channel not supported yet.");
	}

	img_bytes_per_chan = (img_depth == 8) ? 1 : 2;
	img_width_b    = img_width * img_bytes_per_chan; 

	if ( n_chans == 2 ) // for Tiff files channels must be 1 or 3
		temp_n_chans++;

	uint8 *imageData = new uint8[img_height * img_width_b * temp_n_chans];

	if ( img_bytes_per_chan == 1 ) {
		for(sint64 i=0; i<img_height; i++)
		{
			uint8* row_data_8bit = imageData + i*img_width_b*temp_n_chans;
			for(sint64 j=0; j<img_width_b; j++) {
				for ( int c=0; c<n_chans; c++ ) {
					row_data_8bit[j*temp_n_chans + c] = raw_ch_temp[c][(i+start_height)*((sint64)raw_img_width) + (j+start_width)];
				}
				if ( n_chans < temp_n_chans )
					row_data_8bit[j*temp_n_chans + n_chans] = (uint8) 0;
			}
		}
	}
	else {
		uint16  *imageData16   = (uint16 *) imageData;
		uint16 **raw_ch_temp16 = (uint16 **) raw_ch_temp;

		// use img_width instead of img_width_b
		for(sint64 i=0; i<img_height; i++)
		{
			uint16* row_data_16bit = imageData16 + i*img_width*temp_n_chans;
			for(sint64 j=0; j<img_width; j++) { // 
				for ( int c=0; c<n_chans; c++ ) {
					row_data_16bit[j*temp_n_chans + c] = raw_ch_temp16[c][(i+start_height)*((sint64)raw_img_width) + (j+start_width)];
				}
				if ( n_chans < temp_n_chans )
					row_data_16bit[j*temp_n_chans + n_chans] = (uint16) 0;
			}
		}
	}

    //generating complete path for image to be saved
    sprintf(buffer, "%s.%s", img_path.c_str(), TIFF3D_SUFFIX);

	char *err_tiff_fmt;

	if ( do_open )
		iom::IOPluginFactory::getPlugin3D(iom::IMOUT_PLUGIN)->appendSlice(buffer,(unsigned char *)imageData,(int)img_height,(int)img_width,img_bytes_per_chan,temp_n_chans,-1,-1,-1,-1,slice);
	else if ( ((err_tiff_fmt = appendSlice2Tiff3DFile(fhandle,slice,(unsigned char *)imageData,(int)img_width,(int)img_height,temp_n_chans,img_depth,n_pages)) != 0) ) {
		char err_msg[STATIC_STRINGS_SIZE];
		sprintf(err_msg,"in VirtualVolume::saveImage_from_UINT8_to_Tiff3D: error in saving slice %d (%lld x %lld) in file %s (appendSlice2Tiff3DFile: %s)", slice,img_width,img_height,buffer,err_tiff_fmt);
        throw IOException(err_msg);
	};

	delete[] imageData;
}


/*************************************************************************************************************
* Performs downsampling at a halved frequency on the given 3D image.  The given image is overwritten in order
* to store its halvesampled version without allocating any additional resources.
**************************************************************************************************************/
void VirtualVolume::halveSample ( real32* img, int height, int width, int depth, int method )
{
	float A,B,C,D,E,F,G,H;

	// indices are sint64 because offsets can be larger that 2^31 - 1

	if ( method == HALVE_BY_MEAN ) {

		for(sint64 z=0; z<depth/2; z++)
		{
			for(sint64 i=0; i<height/2; i++)
			{
				for(sint64 j=0; j<width/2; j++)
				{
					//computing 8-neighbours
					A = img[2*z*width*height +2*i*width + 2*j];
					B = img[2*z*width*height +2*i*width + (2*j+1)];
					C = img[2*z*width*height +(2*i+1)*width + 2*j];
					D = img[2*z*width*height +(2*i+1)*width + (2*j+1)];
					E = img[(2*z+1)*width*height +2*i*width + 2*j];
					F = img[(2*z+1)*width*height +2*i*width + (2*j+1)];
					G = img[(2*z+1)*width*height +(2*i+1)*width + 2*j];
					H = img[(2*z+1)*width*height +(2*i+1)*width + (2*j+1)];

					//computing mean
					img[z*(width/2)*(height/2)+i*(width/2)+j] = (A+B+C+D+E+F+G+H)/(float)8;
				}
			}
		}

	}
	else if ( method == HALVE_BY_MAX ) {

		for(sint64 z=0; z<depth/2; z++)
		{
			for(sint64 i=0; i<height/2; i++)
			{
				for(sint64 j=0; j<width/2; j++)
				{
					//computing max of 8-neighbours
					A = img[2*z*width*height + 2*i*width + 2*j];
					B = img[2*z*width*height + 2*i*width + (2*j+1)];
					if ( B > A ) A = B;
					B = img[2*z*width*height + (2*i+1)*width + 2*j];
					if ( B > A ) A = B;
					B = img[2*z*width*height + (2*i+1)*width + (2*j+1)];
					if ( B > A ) A = B;
					B = img[(2*z+1)*width*height + 2*i*width + 2*j];
					if ( B > A ) A = B;
					B = img[(2*z+1)*width*height + 2*i*width + (2*j+1)];
					if ( B > A ) A = B;
					B = img[(2*z+1)*width*height + (2*i+1)*width + 2*j];
					if ( B > A ) A = B;
					B = img[(2*z+1)*width*height + (2*i+1)*width + (2*j+1)];
					if ( B > A ) A = B;

					//computing mean
					img[z*(width/2)*(height/2) + i*(width/2) + j] = A;
				}
			}
		}

	}
	else {
		char buffer[STATIC_STRINGS_SIZE];
		sprintf(buffer,"in halveSample(...): invalid halving method\n");
        throw IOException(buffer);
	}
	
}


void VirtualVolume::halveSample_UINT8 ( uint8** img, int height, int width, int depth, int channels, int method, int bytes_chan )
{

	float A,B,C,D,E,F,G,H;

	// indices are sint64 because offsets can be larger that 2^31 - 1

	if ( bytes_chan == 1 ) {

		if ( method == HALVE_BY_MEAN ) {   

			for(sint64 c=0; c<channels; c++)
			{
				for(sint64 z=0; z<depth/2; z++)
				{
					for(sint64 i=0; i<height/2; i++)
					{
						for(sint64 j=0; j<width/2; j++)
						{
							//computing 8-neighbours
							A = img[c][2*z*width*height + 2*i*width + 2*j];
							B = img[c][2*z*width*height + 2*i*width + (2*j+1)];
							C = img[c][2*z*width*height + (2*i+1)*width + 2*j];
							D = img[c][2*z*width*height + (2*i+1)*width + (2*j+1)];
							E = img[c][(2*z+1)*width*height + 2*i*width + 2*j];
							F = img[c][(2*z+1)*width*height + 2*i*width + (2*j+1)];
							G = img[c][(2*z+1)*width*height + (2*i+1)*width + 2*j];
							H = img[c][(2*z+1)*width*height + (2*i+1)*width + (2*j+1)];

							//computing mean
                            img[c][z*(width/2)*(height/2) + i*(width/2) + j] = (uint8) iim::round((A+B+C+D+E+F+G+H)/(float)8);
						}
					}
				}
			}
		}

		else if ( method == HALVE_BY_MAX ) {

		for(sint64 c=0; c<channels; c++)
		{
			for(sint64 z=0; z<depth/2; z++)
			{
				for(sint64 i=0; i<height/2; i++)
				{
					for(sint64 j=0; j<width/2; j++)
					{
						//computing max of 8-neighbours
						A = img[c][2*z*width*height + 2*i*width + 2*j];
						B = img[c][2*z*width*height + 2*i*width + (2*j+1)];
						if ( B > A ) A = B;
						B = img[c][2*z*width*height + (2*i+1)*width + 2*j];
						if ( B > A ) A = B;
						B = img[c][2*z*width*height + (2*i+1)*width + (2*j+1)];
						if ( B > A ) A = B;
						B = img[c][(2*z+1)*width*height + 2*i*width + 2*j];
						if ( B > A ) A = B;
						B = img[c][(2*z+1)*width*height + 2*i*width + (2*j+1)];
						if ( B > A ) A = B;
						B = img[c][(2*z+1)*width*height + (2*i+1)*width + 2*j];
						if ( B > A ) A = B;
						B = img[c][(2*z+1)*width*height + (2*i+1)*width + (2*j+1)];
						if ( B > A ) A = B;

						//computing mean
                        img[c][z*(width/2)*(height/2) + i*(width/2) + j] = (uint8) iim::round(A);
					}
				}
			}
		}

		}
		else {
			char buffer[STATIC_STRINGS_SIZE];
			sprintf(buffer,"in VirtualVolume::halveSample_UINT8(...): invalid halving method\n");
            throw IOException(buffer);
		}

	}
	else if ( bytes_chan == 2 ) {

		uint16 **img16 = (uint16 **) img;

		if ( method == HALVE_BY_MEAN ) {   

			for(sint64 c=0; c<channels; c++)
			{
				for(sint64 z=0; z<depth/2; z++)
				{
					for(sint64 i=0; i<height/2; i++)
					{
						for(sint64 j=0; j<width/2; j++)
						{
							//computing 8-neighbours
							A = img16[c][2*z*width*height + 2*i*width + 2*j];
							B = img16[c][2*z*width*height + 2*i*width + (2*j+1)];
							C = img16[c][2*z*width*height + (2*i+1)*width + 2*j];
							D = img16[c][2*z*width*height + (2*i+1)*width + (2*j+1)];
							E = img16[c][(2*z+1)*width*height + 2*i*width + 2*j];
							F = img16[c][(2*z+1)*width*height + 2*i*width + (2*j+1)];
							G = img16[c][(2*z+1)*width*height + (2*i+1)*width + 2*j];
							H = img16[c][(2*z+1)*width*height + (2*i+1)*width + (2*j+1)];

							//computing mean
                            img16[c][z*(width/2)*(height/2) + i*(width/2) + j] = (uint16) iim::round((A+B+C+D+E+F+G+H)/(float)8);
						}
					}
				}
			}
		}

		else if ( method == HALVE_BY_MAX ) {

		for(sint64 c=0; c<channels; c++)
		{
			for(sint64 z=0; z<depth/2; z++)
			{
				for(sint64 i=0; i<height/2; i++)
				{
					for(sint64 j=0; j<width/2; j++)
					{
						//computing max of 8-neighbours
						A = img16[c][2*z*width*height + 2*i*width + 2*j];
						B = img16[c][2*z*width*height + 2*i*width + (2*j+1)];
						if ( B > A ) A = B;
						B = img16[c][2*z*width*height + (2*i+1)*width + 2*j];
						if ( B > A ) A = B;
						B = img16[c][2*z*width*height + (2*i+1)*width + (2*j+1)];
						if ( B > A ) A = B;
						B = img16[c][(2*z+1)*width*height + 2*i*width + 2*j];
						if ( B > A ) A = B;
						B = img16[c][(2*z+1)*width*height + 2*i*width + (2*j+1)];
						if ( B > A ) A = B;
						B = img16[c][(2*z+1)*width*height + (2*i+1)*width + 2*j];
						if ( B > A ) A = B;
						B = img16[c][(2*z+1)*width*height + (2*i+1)*width + (2*j+1)];
						if ( B > A ) A = B;

						//computing mean
                        img16[c][z*(width/2)*(height/2) + i*(width/2) + j] = (uint16) iim::round(A);
					}
				}
			}
		}

		}
		else {
			char buffer[STATIC_STRINGS_SIZE];
			sprintf(buffer,"in VirtualVolume::halveSample_UINT8(...): invalid halving method\n");
            throw IOException(buffer);
		}

	}
	else {
		char buffer[STATIC_STRINGS_SIZE];
		sprintf(buffer,"VirtualVolume::in halveSample_UINT8(...): invalid number of bytes per channel (%d)\n", bytes_chan);
        throw IOException(buffer);
	}
}

void VirtualVolume::halveSample2D ( real32* img, int height, int width, int depth, int method )
{
	float A,B,C,D;

	// indices are sint64 because offsets can be larger that 2^31 - 1

	if ( method == HALVE_BY_MEAN ) {

		for(sint64 z=0; z<depth; z++)
		{
			for(sint64 i=0; i<height/2; i++)
			{
				for(sint64 j=0; j<width/2; j++)
				{
					//computing 8-neighbours
					A = img[z*width*height +2*i*width + 2*j];
					B = img[z*width*height +2*i*width + (2*j+1)];
					C = img[z*width*height +(2*i+1)*width + 2*j];
					D = img[z*width*height +(2*i+1)*width + (2*j+1)];

					//computing mean
					img[z*(width/2)*(height/2)+i*(width/2)+j] = (A+B+C+D)/(float)4;
				}
			}
		}

	}
	else if ( method == HALVE_BY_MAX ) {

		for(sint64 z=0; z<depth; z++)
		{
			for(sint64 i=0; i<height/2; i++)
			{
				for(sint64 j=0; j<width/2; j++)
				{
					//computing max of 8-neighbours
					A = img[z*width*height + 2*i*width + 2*j];
					B = img[z*width*height + 2*i*width + (2*j+1)];
					if ( B > A ) A = B;
					B = img[z*width*height + (2*i+1)*width + 2*j];
					if ( B > A ) A = B;
					B = img[z*width*height + (2*i+1)*width + (2*j+1)];
					if ( B > A ) A = B;

					//computing max
					img[z*(width/2)*(height/2) + i*(width/2) + j] = A;
				}
			}
		}

	}
	else {
		char buffer[S_STATIC_STRINGS_SIZE];
		sprintf(buffer,"in VirtualVolume::halveSample2D(...): invalid halving method\n");
        throw iom::exception(buffer);
	}
}

void VirtualVolume::halveSample2D_UINT8 ( uint8** img, int height, int width, int depth, int channels, int method, int bytes_chan )
{
	float A,B,C,D;

	// indices are sint64 because offsets can be larger that 2^31 - 1

	if ( bytes_chan == 1 ) {

		if ( method == HALVE_BY_MEAN ) {   

			for(sint64 c=0; c<channels; c++)
			{
				for(sint64 z=0; z<depth; z++)
				{
					for(sint64 i=0; i<height/2; i++)
					{
						for(sint64 j=0; j<width/2; j++)
						{
							//computing 8-neighbours
							A = img[c][z*width*height + 2*i*width + 2*j];
							B = img[c][z*width*height + 2*i*width + (2*j+1)];
							C = img[c][z*width*height + (2*i+1)*width + 2*j];
							D = img[c][z*width*height + (2*i+1)*width + (2*j+1)];

							//computing mean
                            img[c][z*(width/2)*(height/2) + i*(width/2) + j] = (uint8) iim::round((A+B+C+D)/(float)4);
						}
					}
				}
			}
		}

		else if ( method == HALVE_BY_MAX ) {

		for(sint64 c=0; c<channels; c++)
		{
			for(sint64 z=0; z<depth; z++)
			{
				for(sint64 i=0; i<height/2; i++)
				{
					for(sint64 j=0; j<width/2; j++)
					{
						//computing max of 8-neighbours
						A = img[c][z*width*height + 2*i*width + 2*j];
						B = img[c][z*width*height + 2*i*width + (2*j+1)];
						if ( B > A ) A = B;
						B = img[c][z*width*height + (2*i+1)*width + 2*j];
						if ( B > A ) A = B;
						B = img[c][z*width*height + (2*i+1)*width + (2*j+1)];
						if ( B > A ) A = B;

						//computing mean
                        img[c][z*(width/2)*(height/2) + i*(width/2) + j] = (uint8) iim::round(A);
					}
				}
			}
		}

		}
		else {
			char buffer[STATIC_STRINGS_SIZE];
			sprintf(buffer,"in VirtualVolume::halveSample_UINT8(...): invalid halving method\n");
            throw IOException(buffer);
		}

	}
	else if ( bytes_chan == 2 ) {

		uint16 **img16 = (uint16 **) img;

		if ( method == HALVE_BY_MEAN ) {   

			for(sint64 c=0; c<channels; c++)
			{
				for(sint64 z=0; z<depth; z++)
				{
					for(sint64 i=0; i<height/2; i++)
					{
						for(sint64 j=0; j<width/2; j++)
						{
							//computing 8-neighbours
							A = img16[c][z*width*height + 2*i*width + 2*j];
							B = img16[c][z*width*height + 2*i*width + (2*j+1)];
							C = img16[c][z*width*height + (2*i+1)*width + 2*j];
							D = img16[c][z*width*height + (2*i+1)*width + (2*j+1)];

							//computing mean
                            img16[c][z*(width/2)*(height/2) + i*(width/2) + j] = (uint16) iim::round((A+B+C+D)/(float)4);
						}
					}
				}
			}
		}

		else if ( method == HALVE_BY_MAX ) {

			for(sint64 c=0; c<channels; c++)
			{
				for(sint64 z=0; z<depth; z++)
				{
					for(sint64 i=0; i<height/2; i++)
					{
						for(sint64 j=0; j<width/2; j++)
						{
							//computing max of 8-neighbours
							A = img16[c][z*width*height + 2*i*width + 2*j];
							B = img16[c][z*width*height + 2*i*width + (2*j+1)];
							if ( B > A ) A = B;
							B = img16[c][z*width*height + (2*i+1)*width + 2*j];
							if ( B > A ) A = B;
							B = img16[c][z*width*height + (2*i+1)*width + (2*j+1)];
							if ( B > A ) A = B;

							//computing mean
							img16[c][z*(width/2)*(height/2) + i*(width/2) + j] = (uint16) iim::round(A);
						}
					}
				}
			}

		}
		else {
			char buffer[STATIC_STRINGS_SIZE];
			sprintf(buffer,"in VirtualVolume::halveSample2D_UINT8(...): invalid halving method\n");
            throw IOException(buffer);
		}

	}
	else {
		char buffer[STATIC_STRINGS_SIZE];
		sprintf(buffer,"VirtualVolume::in halveSample2D_UINT8(...): invalid number of bytes per channel (%d)\n", bytes_chan);
        throw IOException(buffer);
	}
}

// 2014-04-14. Alessandro. @ADDED 'instance_format' method with inputs = {path, format}.
VirtualVolume* VirtualVolume::instance_format(const char* path, std::string format)
{
    /**/iim::debug(iim::LEV3, strprintf("path = \"%s\", format = \"%s\"", path, format.c_str()).c_str(), __iim__current__function__);
    
    VirtualVolume* volume = 0;
    
    // directory formats
    if(isDirectory(path))
    {
        if((format.compare(TILED_MC_FORMAT) == 0) || (format.compare(TILED_MC_TIF3D_FORMAT) == 0))
            volume = new TiledMCVolume(path);
        else if(format.compare(STACKED_FORMAT) == 0)
            volume = new StackedVolume(path);
        else if((format.compare(TILED_FORMAT) == 0) || (format.compare(TILED_TIF3D_FORMAT) == 0))
            volume = new TiledVolume(path);
        else if(format.compare(SIMPLE_RAW_FORMAT) == 0)
            volume = new SimpleVolumeRaw(path);
        else if(format.compare(SIMPLE_FORMAT) == 0)
            volume = new SimpleVolume(path);
        else if(format.compare(TIME_SERIES) == 0)
            volume = new TimeSeries(path);
        else
            throw IOException(strprintf("in VirtualVolume::instance(): Unsupported format \"%s\" for path \"%s\" which is a directory", format.c_str(), path), __iim__current__function__);
    }
    // file formats
    else if(isFile(path))
    {
        if(format.compare(RAW_FORMAT) == 0 || (format.compare(TIF3D_FORMAT) == 0))
            volume = new RawVolume(path);
		else if(format.compare(UNST_TIF3D_FORMAT) == 0)
            volume = new UnstitchedVolume(path);
		else if(format.compare(MULTISLICE_FORMAT) == 0)
            volume = new MultiSliceVolume(path);
		else if(format.compare(MULTICYCLE_FORMAT) == 0)
            volume = new MultiCycleVolume(path);
		else if(format.compare(BDV_HDF5_FORMAT) == 0)
            throw IOException(strprintf("in VirtualVolume::instance(): volumes in format \"%s\" should be created with another \"instance\" method", format.c_str(), path), __iim__current__function__);
        else
            throw IOException(strprintf("in VirtualVolume::instance(): Unsupported format \"%s\" for path \"%s\" which is a file", format.c_str(), path), __iim__current__function__);
    }
    else
        throw IOException(strprintf("in VirtualVolume::instance(): path = \"%s\" does not exist", path), __iim__current__function__);
    
    return volume;
}

// tries to automatically detect the volume format and returns the imported volume if succeeds (otherwise returns 0)
// WARNING: all metadata files (if needed by that format) are assumed to be present. Otherwise, that format will be skipped.
VirtualVolume* VirtualVolume::instance(const char* path)
{
    /**/iim::debug(iim::LEV3, strprintf("path = \"%s\"", path).c_str(), __iim__current__function__);

    VirtualVolume* volume = 0;

    // directory formats
    if(isDirectory(path))
    {
        // 2015-02-06. Alessandro. @ADDED volume format metadata file for faster opening of a "directory format"
        std::string format = "unknown";
        if(isFile(std::string(path) + "/" + iim::FORMAT_MDATA_FILE_NAME))
        {
            try
            {
                std::ifstream f((std::string(path) + "/" + iim::FORMAT_MDATA_FILE_NAME).c_str());
                if(f.is_open())
                {
                    std::getline(f,format);
                    f.close();
                    
                    // 2015-04-14. Alessandro. @FIXED detection of volume format from .iim.format file
                    if(format.compare((TiledMCVolume().getPrintableFormat())) == 0)
                        volume = new TiledMCVolume(path);
                    else if(format.compare((StackedVolume().getPrintableFormat())) == 0)
                        volume = new StackedVolume(path);
                    else if(format.compare((TiledVolume().getPrintableFormat())) == 0)
                        volume = new TiledVolume(path);
                    else if(format.compare((SimpleVolume().getPrintableFormat())) == 0)
                        volume = new SimpleVolume(path);
                    else if(format.compare((SimpleVolumeRaw().getPrintableFormat())) == 0)
                        volume = new SimpleVolumeRaw(path);
                    else if(format.compare((TimeSeries().getPrintableFormat())) == 0)
                        volume = new TimeSeries(path);
                    else
                        iim::warning(iim::strprintf("Cannot recognize format \"%s\"", format.c_str()).c_str(), __iim__current__function__);
                }
            }
            catch(IOException &ex)
            {
                debug(LEV3, strprintf("Cannot import <%s> format at \"%s\": %s", format.c_str(), path, ex.what()).c_str(),__iim__current__function__);
            }
        }

        if(!volume)
        {
            try
            {
                volume = new TiledMCVolume(path);
            }
            catch(IOException &ex)
            {
                debug(LEV3, strprintf("Cannot import <TiledMCVolume> at \"%s\": %s", path, ex.what()).c_str(),__iim__current__function__);
                try
                {
                    volume = new StackedVolume(path);
                }
                catch(IOException &ex)
                {
                    debug(LEV3, strprintf("Cannot import <StackedVolume> at \"%s\": %s", path, ex.what()).c_str(),__iim__current__function__);
                    try
                    {
                        volume = new TiledVolume(path);
                    }
                    catch(IOException &ex)
                    {
                        debug(LEV3, strprintf("Cannot import <TiledVolume> at \"%s\": %s", path, ex.what()).c_str(),__iim__current__function__);
                        try
                        {
                            volume = new SimpleVolume(path);
                        }
                        catch(IOException &ex)
                        {
                            debug(LEV3, strprintf("Cannot import <SimpleVolume> at \"%s\": %s", path, ex.what()).c_str(),__iim__current__function__);
                            try
                            {
                                volume = new SimpleVolumeRaw(path);
                            }
                            catch(IOException &ex)
                            {
                                debug(LEV3, strprintf("Cannot import <SimpleVolumeRaw> at \"%s\": %s", path, ex.what()).c_str(),__iim__current__function__);
                                try
                                {
                                    volume = new TimeSeries(path);
                                }
                                catch(IOException &ex)
                                {
                                    debug(LEV3, strprintf("Cannot import <TimeSeries> at \"%s\": %s", path, ex.what()).c_str(),__iim__current__function__);
                                }
                            }
                        }
                    }
                }
            }
            catch(...)
            {
                debug(LEV3, strprintf("generic error occurred when importing volume at \"%s\"", path).c_str(),__iim__current__function__);
            }
        }

        // 2015-04-14. Alessandro. @FIXED detection of volume format from .iim.format file
        // 2015-02-06. Alessandro. @ADDED volume format metadata file for faster opening of a "directory format"
        if(volume && !isFile(std::string(path) + "/" + iim::FORMAT_MDATA_FILE_NAME))
        {
            std::ofstream f((std::string(path) + "/" + iim::FORMAT_MDATA_FILE_NAME).c_str(), std::ofstream::out);
            if(f.is_open())
            {
                f << volume->getPrintableFormat();
                f.close();
            }
        }
    }
    // try all file formats
    else if(isFile(path))
    {
        if(     iim::hasEnding(path, ".raw") ||
                iim::hasEnding(path, ".v3draw") ||
                iim::hasEnding(path, ".RAW") ||
                iim::hasEnding(path, ".V3DRAW") ||
                iim::hasEnding(path, ".tif") ||
                iim::hasEnding(path, ".tiff") ||
                iim::hasEnding(path, ".TIF") ||
                iim::hasEnding(path, ".TIFF"))
            volume = new RawVolume(path);
        else if(iim::hasEnding(path, ".xml") || iim::hasEnding(path, ".XML"))
            volume = new UnstitchedVolume(path);
        else
            throw IOException(strprintf("Unsupported file extensions for \"%s\"", path).c_str(),__iim__current__function__);
    }
    else
        throw IOException(strprintf("Path = \"%s\" does not exist", path), __iim__current__function__);

    return volume;
}


// this version if "instance" methods should be used to create a BDVVolume since at least the 'res' parameter is needed
VirtualVolume* VirtualVolume::instance(const char* fname, int res, void *descr, int timepoint)  {
    /**/iim::debug(iim::LEV3, strprintf("fname = \"%s\", res = %d, descr = %p", fname, res, descr).c_str(), __iim__current__function__);

	return new BDVVolume(fname,res,timepoint,descr); 
}


// returns the imported volume if succeeds (otherwise returns 0)
// WARNING: no assumption is made on metadata files, which are possibly (re-)generated using the additional informations provided.
VirtualVolume* VirtualVolume::instance(const char* path, std::string format,
                                       iim::axis AXS_1, iim::axis AXS_2, iim::axis AXS_3, /* = iim::axis_invalid */
                                       float VXL_1 /* = 0 */, float VXL_2 /* = 0 */, float VXL_3 /* = 0 */) 
{
    /**/iim::debug(iim::LEV3, strprintf("path = \"%s\", format = %s, AXS_1 = %s, AXS_2 = %s, AXS_3 = %s, VXL_1 = %.2f, VXL_2 = %.2f, VXL_3 = %.2f",
                                        path, format.c_str(), axis_to_str(AXS_1), axis_to_str(AXS_2), axis_to_str(AXS_3),
                                        VXL_1, VXL_2, VXL_3).c_str(), __iim__current__function__);

    VirtualVolume* volume = 0;

    // directory formats
    if(isDirectory(path))
    {
        if((format.compare(TILED_MC_FORMAT) == 0) || (format.compare(TILED_MC_TIF3D_FORMAT) == 0))
        {
            if(AXS_1 != axis_invalid && AXS_2 != axis_invalid && AXS_3 != axis_invalid && VXL_1 != 0 && VXL_2 != 0 && VXL_3 != 0)
                volume = new TiledMCVolume(path, ref_sys(AXS_1,AXS_2,AXS_3), VXL_1, VXL_2, VXL_3, true, true);
            else
                throw IOException(strprintf("Invalid parameters AXS_1(%s), AXS_2(%s), AXS_3(%s), VXL_1(%.2f), VXL_2(%.2f), VXL_3(%.2f)",
                                            axis_to_str(AXS_1), axis_to_str(AXS_2), axis_to_str(AXS_3), VXL_1, VXL_2, VXL_3).c_str());
        }
        else if(format.compare(STACKED_FORMAT) == 0)
        {
            if(AXS_1 != axis_invalid && AXS_2 != axis_invalid && AXS_3 != axis_invalid && VXL_1 != 0 && VXL_2 != 0 && VXL_3 != 0)
                volume = new StackedVolume(path, ref_sys(AXS_1,AXS_2,AXS_3), VXL_1, VXL_2, VXL_3, true, true);
            else
                throw IOException(strprintf("Invalid parameters AXS_1(%s), AXS_2(%s), AXS_3(%s), VXL_1(%.2f), VXL_2(%.2f), VXL_3(%.2f)",
                                            axis_to_str(AXS_1), axis_to_str(AXS_2), axis_to_str(AXS_3), VXL_1, VXL_2, VXL_3).c_str());
        }
        else if((format.compare(TILED_FORMAT) == 0) || (format.compare(TILED_TIF3D_FORMAT) == 0))
        {
            if(AXS_1 != axis_invalid && AXS_2 != axis_invalid && AXS_3 != axis_invalid && VXL_1 != 0 && VXL_2 != 0 && VXL_3 != 0)
                volume = new TiledVolume(path, ref_sys(AXS_1,AXS_2,AXS_3), VXL_1, VXL_2, VXL_3, true, true);
            else
                throw IOException(strprintf("Invalid parameters AXS_1(%s), AXS_2(%s), AXS_3(%s), VXL_1(%.2f), VXL_2(%.2f), VXL_3(%.2f)",
                                            axis_to_str(AXS_1), axis_to_str(AXS_2), axis_to_str(AXS_3), VXL_1, VXL_2, VXL_3).c_str());
        }
        else if(format.compare(SIMPLE_RAW_FORMAT) == 0)
            volume = new SimpleVolumeRaw(path);
        else if(format.compare(SIMPLE_FORMAT) == 0)
            volume = new SimpleVolume(path);
		else if(format.compare(TIME_SERIES) == 0)
			volume = new TimeSeries(path);
       else
            throw IOException(strprintf("in VirtualVolume::instance(): Unsupported format \"%s\" for path \"%s\" which is a directory", format.c_str(), path), __iim__current__function__);
    }
    // file formats
    else if(isFile(path))
    {
        if(format.compare(RAW_FORMAT) == 0  || (format.compare(TIF3D_FORMAT) == 0))
            volume = new RawVolume(path);
		else if(format.compare(UNST_TIF3D_FORMAT) == 0)
            volume = new UnstitchedVolume(path);
		else if(format.compare(BDV_HDF5_FORMAT) == 0)
            throw IOException(strprintf("in VirtualVolume::instance(): volumes in format \"%s\" should be created with another \"instance\" method", format.c_str(), path), __iim__current__function__);
		else
            throw IOException(strprintf("in VirtualVolume::instance(): Unsupported format \"%s\" for path \"%s\" which is a file", format.c_str(), path), __iim__current__function__);
    }
    else
        throw IOException(strprintf("in VirtualVolume::instance(): path = \"%s\" does not exist", path), __iim__current__function__);


    return volume;
}

// (@MOVED from TiledMCVolume.cpp by Alessandro on 2014-02-20)
// WARNING: caller loses ownership of array '_active' 
void VirtualVolume::setActiveChannels ( uint32 *_active, int _n_active )
{
    /**/iim::debug(iim::LEV3, 0, __iim__current__function__);

    if ( active )
        delete[] active;
    active   = _active;
    n_active = _n_active;
}

// returns true if the given format is hierarchical, i.e. if it consists of nested folders (1 level at least)
bool VirtualVolume::isHierarchical(std::string format)
{
    /**/iim::debug(iim::LEV3, strprintf("format = %s", format.c_str()).c_str(), __iim__current__function__);

    if(format.compare(TILED_FORMAT) == 0)
        return true;
    else if(format.compare(TILED_MC_FORMAT) == 0)
        return true;
     else if(format.compare(TILED_MC_TIF3D_FORMAT) == 0)
        return true;
   else if(format.compare(STACKED_FORMAT) == 0)
        return true;
    else if(format.compare(TIME_SERIES) == 0)
        return true;
    else if(format.compare(SIMPLE_FORMAT) == 0)
        return true;
    else if(format.compare(SIMPLE_RAW_FORMAT) == 0)
        return true;
    else if(format.compare(TILED_TIF3D_FORMAT) == 0)
        return true;
    else if(format.compare(RAW_FORMAT) == 0)
        return false;
    else if(format.compare(TIF3D_FORMAT) == 0)
        return false;
     else if(format.compare(UNST_TIF3D_FORMAT) == 0)
        return false;
    else if(format.compare(BDV_HDF5_FORMAT) == 0)
        return false;
   else
        throw IOException(strprintf("Unsupported format %s", format.c_str()), __iim__current__function__);

}
