// Avisynth v2.5.  Copyright 2002 Ben Rudiak-Gould et al.
// http://www.avisynth.org

// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA, or visit
// http://www.gnu.org/copyleft/gpl.html .
//
// Linking Avisynth statically or dynamically with other modules is making a
// combined work based on Avisynth.  Thus, the terms and conditions of the GNU
// General Public License cover the whole combination.
//
// As a special exception, the copyright holders of Avisynth give you
// permission to link Avisynth with independent modules that communicate with
// Avisynth solely through the interfaces defined in avisynth.h, regardless of the license
// terms of these independent modules, and to copy and distribute the
// resulting combined work under terms of your choice, provided that
// every copy of the combined work is accompanied by a complete copy of
// the source code of Avisynth (the version of Avisynth used to produce the
// combined work), being distributed under the terms of the GNU General
// Public License plus this exception.  An independent module is a module
// which is not derived from or based on Avisynth, such as 3rd-party filters,
// import and export plugins, or graphical user interfaces.



#include "stdafx.h"

#include "ImageSeq.h"




/********************************************************************
***** Declare index of new filters for Avisynth's filter engine *****
********************************************************************/

AVSFunction Image_filters[] = {
  { "ImageWriter", "c[file]s[start]i[end]i[type]s[info]b", ImageWriter::Create }, 
    // clip, base filename, start, end, image format/extension
  { "ImageReader", "[file]s[start]i[end]i[fps]i[use_devil]b", ImageReader::Create }, 
    // base filename (sprintf-style), start, end, frames per second, default reader to use
  { 0 }
};



/*****************************
 *******   Image Writer ******
 ****************************/

ImageWriter::ImageWriter(PClip _child, const char * _base_name, const int _start, const int _end,
                         const char * _ext, bool _info)
 : GenericVideoFilter(_child), antialiaser(vi.width, vi.height, "Arial", 192), base_name(_base_name), start(_start),
    end(_end), ext(_ext), info(_info)
{  
  if (!lstrcmpi(ext, "ebmp")) 
  {
    // construct file header  
    fileHeader.bfType = ('M' << 8) + 'B'; // I hate little-endian
    fileHeader.bfSize = vi.BMPSize(); // includes 4-byte padding
    fileHeader.bfReserved1 = 0;
    fileHeader.bfReserved2 = 0;
    fileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    fileHeader.bfSize += fileHeader.bfOffBits;
  
    // construct info header
    infoHeader.biSize = sizeof(BITMAPINFOHEADER);
    infoHeader.biWidth = vi.width;
    infoHeader.biHeight = vi.height;
    infoHeader.biPlanes = vi.IsPlanar() ? 3 : 1;
    infoHeader.biBitCount = vi.BitsPerPixel();
    infoHeader.biCompression = 0;
    infoHeader.biSizeImage = fileHeader.bfSize - fileHeader.bfOffBits;
    infoHeader.biXPelsPerMeter = 0;
    infoHeader.biYPelsPerMeter = 0;
    infoHeader.biClrUsed = 0;
    infoHeader.biClrImportant = 0;
  }
  else {
    ilInit();
  } 
}


ImageWriter::~ImageWriter()
{
  if (!lstrcmpi(ext, "ebmp"))
  {
  }
  else {
    ilShutDown();
  }
}



PVideoFrame ImageWriter::GetFrame(int n, IScriptEnvironment* env) 
{
  PVideoFrame frame = child->GetFrame(n, env);
  
  // check bounds (where end=0 implies no upper bound)
  if (n < start || (end > 0 && n > end) )
    return frame;  

  // construct filename
  ostringstream fn_oss;
  fn_oss << base_name << setfill('0') << setw(6) << n << '.' << ext;
  string filename = fn_oss.str();

  if (!lstrcmpi(ext, "ebmp"))  /* Use internal 'ebmp' writer */
  {
    // initialize file object
    ofstream file(filename.c_str(), ios::out | ios::trunc | ios::binary);  
    if (!file)
      env->ThrowError("ImageWriter: could not create file");

    // write headers
    file.write(reinterpret_cast<const char *>( &fileHeader ), sizeof(BITMAPFILEHEADER));
    file.write(reinterpret_cast<const char *>( &infoHeader ), sizeof(BITMAPINFOHEADER));
    
    // write raster
    const BYTE * srcPtr = frame->GetReadPtr();
    int pitch = frame->GetPitch(); 
    int row_size = frame->GetRowSize();
    int height = frame->GetHeight();    
    
    fileWrite(file, srcPtr, pitch, row_size, height);

    if (vi.IsYV12())
    {
      srcPtr = frame->GetReadPtr(PLANAR_U);
      pitch = frame->GetPitch(PLANAR_U); 
      row_size = frame->GetRowSize(PLANAR_U);
      height = frame->GetHeight(PLANAR_U);
      fileWrite(file, srcPtr, pitch, row_size, height);

      srcPtr = frame->GetReadPtr(PLANAR_V);
      fileWrite(file, srcPtr, pitch, row_size, height);
    }

    // clean up
    file.close();
  }
  else { /* Use DevIL library */

    // Check colorspace
    if (!vi.IsRGB24())
      env->ThrowError("ImageWriter: DevIL requires RGB24 input");

    // Set up DevIL    
    ILuint myImage;
    ilGenImages(1, &myImage);
    ilBindImage(myImage);
    
    // Set image parameters
    ilTexImage(vi.width, vi.height, 1, vi.BitsPerPixel() / 8, IL_BGR, IL_UNSIGNED_BYTE, NULL);

    // Program actual image raster
    const BYTE * srcPtr = frame->GetReadPtr();
    int pitch = frame->GetPitch();
    for (int y=0; y<vi.height; ++y)
    {
      ilSetPixels(0, y, 0, vi.width, 1, 1, IL_BGR, IL_UNSIGNED_BYTE, (void*) srcPtr);
      srcPtr += pitch;
    }

    // DevIL writer fails if the file exists, so delete first
    DeleteFile(filename.c_str());
    
    // Save to disk (format automatically inferred from extension)
    ilSaveImage(const_cast<char * const> (filename.c_str()) );

    // Get errors if any
    ILenum err = ilGetError();
    if (err != IL_NO_ERROR)
      env->ThrowError("ImageWriter: error '%s' in DevIL library writing file %s", 
                        getErrStr(err).c_str(), filename.c_str());
    
    // Clean up
    ilDeleteImages(1, &myImage);
  }  
    
  if (info) {
    // overlay on video output: progress indicator
    ostringstream text;
    text << "Frame " << n << " written to: " << filename;

    HDC hdc = antialiaser.GetDC();
    RECT r = { 32, 16, min(3440,vi.width*8), 768*2 };
    DrawText(hdc, text.str().c_str(), -1, &r, 0);
    GdiFlush();

    env->MakeWritable(&frame);
    antialiaser.Apply(vi, &frame, frame->GetPitch(),
      vi.IsYUV() ? 0xD21092 : 0xFFFF00, vi.IsYUV() ? 0x108080 : 0);
  }
  
  return frame;
}


void ImageWriter::fileWrite(ostream & file, const BYTE * srcPtr, const int pitch, const int row_size, const int height)
{
  int dummy = 0;      
  int padding = (4 - (row_size % 4)) % 4;

  for(UINT i=0; i < height; ++i)
  {
    file.write(reinterpret_cast<const char *>( srcPtr ), row_size);
    file.write(reinterpret_cast<char *>( &dummy ), padding); // pad with 0's to mod-4
    srcPtr += pitch;
  }
}


AVSValue __cdecl ImageWriter::Create(AVSValue args, void*, IScriptEnvironment* env) 
{
  return new ImageWriter(args[0].AsClip(), args[1].AsString("c:\\"), args[2].AsInt(0), args[3].AsInt(0),
                         args[4].AsString("ebmp"), args[5].AsBool(false));
}




/*****************************
 *******   Image Reader ******
 ****************************/

ImageReader::ImageReader(const char * _base_name, const int _start, const int _end, const int _fps, bool _use_DevIL)
 : base_name(_base_name), start(_start), end(_end), fps(_fps), use_DevIL(_use_DevIL)
{
  // Generate full name
  sprintf(fileName, base_name, start);
    
  // Try to parse as bmp/ebmp
  ifstream file(fileName, ios::binary);  
  file.read( reinterpret_cast<char *> (&fileHeader), sizeof(fileHeader) );
  file.read( reinterpret_cast<char *> (&infoHeader), sizeof(infoHeader) );
  file.close();

  if ( fileHeader.bfType == ('M' << 8) + 'B' && use_DevIL == false)
  {
    use_DevIL = false;

    vi.width = infoHeader.biWidth;
    vi.height = infoHeader.biHeight;
    vi.fps_numerator = fps;
    vi.fps_denominator = 1;
    vi.num_frames = end + 1;  // make sure each frame can be requested
    vi.audio_samples_per_second = 0;
    
    if (infoHeader.biBitCount == 32) {
      vi.pixel_type = VideoInfo::CS_BGR32;      
    } else if (infoHeader.biBitCount == 24) {
      vi.pixel_type = VideoInfo::CS_BGR24;
    } else if (infoHeader.biBitCount == 16) {
      vi.pixel_type = VideoInfo::CS_YUY2;
    } else if (infoHeader.biBitCount == 12) {
      vi.pixel_type = VideoInfo::CS_YV12;
    }
  }
  else {  // attempt to open via DevIL
    use_DevIL = true;

    ilInit();
    
    ILuint myImage;
    ilGenImages(1, &myImage);
    ilBindImage(myImage);

    ilLoadImage(fileName);
    
    // no error checking...

    vi.width = ilGetInteger(IL_IMAGE_WIDTH);
    vi.height = ilGetInteger(IL_IMAGE_HEIGHT);
    vi.fps_numerator = fps;
    vi.fps_denominator = 1;
    vi.num_frames = end + 1;
    vi.audio_samples_per_second = 0;
    vi.pixel_type = VideoInfo::CS_BGR24;

    ilDeleteImages(1, &myImage);
  }
}


ImageReader::~ImageReader()
{
  if (use_DevIL) {
    ilShutDown();
  }
}



PVideoFrame ImageReader::GetFrame(int n, IScriptEnvironment* env) 
{
  PVideoFrame frame = env->NewVideoFrame(vi);
  BYTE * dstPtr = frame->GetWritePtr();
  
  int pitch = frame->GetPitch();
  int row_size = frame->GetRowSize();
  int height = frame->GetHeight();
  int width = vi.width;

  sprintf(fileName, base_name, n);
  
  // check range
  if (n < start || n > end) {
    memset(dstPtr, 0, frame->GetPitch() * frame->GetHeight());
    return frame;
  }  

  if (use_DevIL)  /* read using DevIL */
  {    
    // Setup
    ILuint myImage;
    ilGenImages(1, &myImage);
    ilBindImage(myImage);

    ilLoadImage(fileName);

    // Get errors if any
    ILenum err = ilGetError();
    if (err != IL_NO_ERROR)
      env->ThrowError("ImageReader: error '%s' in DevIL library reading file %s", 
                        getErrStr(err).c_str(), fileName);


    // Check some parameters
    if ( ilGetInteger(IL_IMAGE_HEIGHT) != height)
      env->ThrowError("ImageReader: images must have identical heights");
    if ( ilGetInteger(IL_IMAGE_WIDTH) != width)
      env->ThrowError("ImageReader: images must have identical widths");

    // Copy raster to AVS frame
    for (int y=0; y<height; ++y)
    {
      ilCopyPixels(0, y, 0, width, 1, 1, IL_BGR, IL_UNSIGNED_BYTE, dstPtr);
      dstPtr += pitch;
    }

    // Get errors if any    
    err = ilGetError();
    if (err != IL_NO_ERROR)
      env->ThrowError("ImageReader: error '%s' in DevIL library reading file %s", 
                        getErrStr(err).c_str(), fileName);

    // Cleanup
    ilDeleteImages(1, &myImage);
  }
  else {  /* treat as ebmp  */
    // Open file, ensure it has the expected properties
    ifstream file(fileName, ios::binary);
    checkProperties(file, env);
    
    // Read in raster, seeking past padding
    file.seekg (fileHeader.bfOffBits, ios::beg); 
    fileRead(file, dstPtr, pitch, row_size, height);

    if (vi.IsYV12())
    {
      dstPtr = frame->GetWritePtr(PLANAR_U);
      pitch = frame->GetPitch(PLANAR_U); 
      row_size = frame->GetRowSize(PLANAR_U);
      height = frame->GetHeight(PLANAR_U);
      fileRead(file, dstPtr, pitch, row_size, height);

      dstPtr = frame->GetWritePtr(PLANAR_V);
      fileRead(file, dstPtr, pitch, row_size, height);
    }      

    file.close();
  }

  return frame;
}


void ImageReader::fileRead(istream & file, BYTE * dstPtr, const int pitch, const int row_size, const int height)
{
  int padding = (4 - (row_size % 4)) % 4;
  for (int y=0; y<height; ++y) 
  {
    file.read( reinterpret_cast<char *> (dstPtr), row_size);
    file.seekg(padding, ios_base::cur);
    dstPtr += pitch;
  }
}


void ImageReader::checkProperties(istream & file, IScriptEnvironment * env)
{
  BITMAPFILEHEADER tempFileHeader;
  BITMAPINFOHEADER tempInfoHeader;
  
  file.read( reinterpret_cast<char *> (&tempFileHeader), sizeof(tempFileHeader) );
  file.read( reinterpret_cast<char *> (&tempInfoHeader), sizeof(tempInfoHeader) );
 
  if (tempFileHeader.bfType != fileHeader.bfType)
    env->ThrowError("ImageReader: invalid (E)BMP file");  
  if (tempInfoHeader.biWidth != infoHeader.biWidth)
    env->ThrowError("ImageReader: image widths must be identical");
  if (tempInfoHeader.biHeight != infoHeader.biHeight)
    env->ThrowError("ImageReader: image heights must be identical");
  if (tempInfoHeader.biPlanes != infoHeader.biPlanes)
    env->ThrowError("ImageReader: images must have the same number of planes");
  if (tempInfoHeader.biBitCount != infoHeader.biBitCount)
    env->ThrowError("ImageReader: images must have identical bits per pixel");
  if (tempFileHeader.bfSize != fileHeader.bfSize)
    env->ThrowError("ImageReader: raster sizes must be identical");
  if (tempInfoHeader.biCompression != 0)
    env->ThrowError("ImageReader: EBMP reader cannot handle compressed images"); 
}   


AVSValue __cdecl ImageReader::Create(AVSValue args, void*, IScriptEnvironment* env) 
{
  return new ImageReader(args[0].AsString("c:\\"), args[1].AsInt(0), args[2].AsInt(1000), args[3].AsInt(24), 
                         args[4].AsBool(false));
}


string getErrStr(ILenum err)
{  
  if (err == IL_INVALID_ENUM)
    return "Invalid Enum";
  if (err == IL_OUT_OF_MEMORY)
    return "Out of memory";
  if (err == IL_FORMAT_NOT_SUPPORTED)
    return "Format not supported";
  if (err == IL_INTERNAL_ERROR)
    return "Internal error";
  if (err == IL_INVALID_VALUE)
    return "Invalid value";
  if (err == IL_ILLEGAL_OPERATION)
    return "Illegal operation";
  if (err == IL_ILLEGAL_FILE_VALUE)
    return "Illegal file";
  if (err == IL_INVALID_FILE_HEADER)
    return "Illegal file header";
  if (err == IL_COULD_NOT_OPEN_FILE)
    return "Could not open file";
  if (err == IL_INVALID_EXTENSION)
    return "Invalid extension";
  if (err == IL_FILE_ALREADY_EXISTS)
    return "File already exists";
  if (err == IL_OUT_FORMAT_SAME)
    return "Output format same";
  if (err == IL_STACK_OVERFLOW)
    return "Stack overflow";
  if (err == IL_STACK_UNDERFLOW)
    return "Stack underflow";
  if (err == IL_INVALID_CONVERSION)
    return "Invalid conversion";
  if (err == IL_BAD_DIMENSIONS)
    return "Bad dimensions";
  if (err == IL_FILE_READ_ERROR)
    return "File read error";
  if (err == IL_FILE_WRITE_ERROR)
    return "File write error";
  if (err == IL_LIB_GIF_ERROR)
    return "LibGif error";
  if (err == IL_LIB_JPEG_ERROR)
    return "LifJpeg error";
  if (err == IL_LIB_PNG_ERROR)
    return "LibPng error";
  if (err == IL_LIB_TIFF_ERROR)
    return "LibTiff error";
  if (err == IL_LIB_MNG_ERROR)
    return "LibMng error";
  if (err == IL_UNKNOWN_ERROR)
    return "Unknown error";

  return "Unknown error";
}
