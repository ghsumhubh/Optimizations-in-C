/* 206534299 Ido Tziony
 *
 * I've compiled all my optimization and divided the explanation into 4 sections:
 * 1) Next to each function/code I refer to the speed-ups I've used.
 * 2) Ideas I had that didn't work
 * 3) Ideas I had that did work -> **most important section**
 * 4) Tools I used
 *
 *----------------------------------------------------------------------------------------------------------------------------
 * Ideas I had that didn't work:
 * 1) Threading
 * pthread (tried it) and split smooth into 2 parts - doesn't work since I can't link it using this file only
 * For passing the arguments to the threads I created a new struct for all the info needed for the functions it runs.
 * tried: #pragma comment(lib, "libpthread.so"), #pragma comment(lib, "libpthread.so.0") but I suspect since it's a shared
 * library it didn't work.
 *
 * 2) target the architecture
 * I went ahead and typed lscpu in MOBAX and got:
 * Model name:            Intel(R) Xeon(R) CPU E5-2630 v4 @ 2.20GHz
 * which is a Broadwell based CPU
 * tried #pragma GCC target ("arch=broadwell")
 * and also #pragma GCC target ("arch=sandybridge") and it didn't recognize any of those (even though I've seen codes that employ arch=sandybridge
 * which is why I tried that too.
 *
 * 3) using evil C code
 *    *((unsigned long *) pixels)= *((unsigned long *)pixelPointer);
 *     pixels[2] = *(pixelPointer+2);
 *    *((unsigned long *) ((pixel*) pixels+3))= *((unsigned long *)((pixel*)pixelPointer+dim));
 *    pixels[5] = *(pixelPointer+dim+2);
 *    *((unsigned long *) ((pixel*) pixels+6))= *((unsigned long *)((pixel*)pixelPointer+2*dim));
 *    pixels[8] = *(pixelPointer+2*dim+2);
 *
 * instead of
 *    pixels[0] = *pixelPointer;
 *    pixels[1] = *(pixelPointer+1);
 *    pixels[2] = *(pixelPointer+2);
 *    pixels[3] = *(pixelPointer+dim);
 *    pixels[4]=  *(pixelPointer+dim+1);
 *    pixels[5]=  *(pixelPointer+dim+2);
 *    pixels[6]=  *(pixelPointer+2*dim);
 *    pixels[7]=  *(pixelPointer+2*dim+1);
 *    pixels[8]=  *(pixelPointer+2*dim+2);
 *
 *    which DID work but provided me a slower code.
 *
 *    4) changing writeBMP in my code-  was later discovered that it's nto allowed.
 *
 *    5) Loop inversion. I know it runs faster on most CPU's, but it might have made the code uses more arithmetic elsewhere, and since the loop is already unrolled I suspect loop inversion won't give a breakthrough result.
 *
 * ---------------------------------------------------------------------------------------------------------
 *
 *    Ideas I had that did work:
 *
 *    1) using GCC target + Optimize to optimize the code using the compiler (like we studied in class)
 *    since it can do things like fetching addresses before we need them, which I am unable to do using C
 *    Furthermore since I knew the target CPU I could deduce I can use avx,avx2,sse,abm,bmi,bmi2 which are included in that CPU
 *
 *    2) Understanding the code -> what parts are de-facto constants like kernelSize=3 and which parts are omittable.
 *    For example: if we are blurring, we are in essence averaging 7 or 9 pixels so there is no need for a negativity check in the sum.
 *
 *    3) Reduced function calls and function arguments. Speaks for itself, I removed function calls and omitted arguments that were implied
 *
 *    4) Split functions into 2. In order to avoid If statements which are in actuality always passed through and waste CPU time if it needs to revert in case of false
 *    I split functions into the variants of the function, e.g., "applyBlurKernel(), applyBlurKernelWithFilter(), applySharpenKernel().
 *    This not only saved the branching problem but also help localize the code, since I can sum multiple things locally if I know I need to use the filter.
 *
 *    5) Loop unrolling (Self explanatory) - was only done in inner loop to avoid too big of a code that will take more time since will need to load more memory pages.
 *
 *    6) Removed unnecessary casting / unreachable branches / unused vars etc.
 *
 *    7) Copying memory using unsigned long instead of the given data structure
 *
 *    8) Using ++var instead of var++
 *
 *    9) Special optimization of some code segments based on the assumption a big enough % of images will have 3x3 of black/white, so we could speed it up.
 *    This was only implemented on a function that already had all the information to deduce that and that had a lot of work left still afterwards.
 *
 *    10) using pointers and the ++ instead of array[i] wherever it fits nicely.
 *
 *
 *
 *    ------------------------------------------------------------------------------------------------------------------------------------
 *    Tools I used:
 *    1) PROFILING: I started by adding the -pg tag to the make file and running gprof, this gave me limited information, so I swapped to valgrind and KCachegrind and used
 *    valgrind --tool=callgrind --dump-instr=yes --simulate-cache=yes --collect-jumps=yes ./showBMP gibson_500.bmp 1/0
 *    to run the profiling.
 *    2) TESTER: I used the tester built by Ron Even to check my relative time compared to others (Since valgrind was local and couldn't predict my performance comparatively)

*/




// could in theory remove it for O(1) less time, but in reality I really liked the neat matrices, so I will keep them!
#define KERNEL_SIZE 3
// disable run-time bound checking since I already tested my code and it's ready for prod.
#undef _GLIBCXX_DEBUG
// add vectorization option to GCC
#pragma GCC target ("avx,avx2,sse")
// add bit manipulation option to GCC
#pragma GCC target ("abm,bmi,bmi2")
// optimization flags for GCC
#pragma GCC optimize("Ofast,inline")

// dependencies
#include <stdbool.h>
#include "readBMP.h"
#include "writeBMP.h"
#include <stdlib.h>


// Only initializes once, not a problem if we declare here
int blurKernel[KERNEL_SIZE][KERNEL_SIZE] = {{1, 1, 1}, {1, 1, 1}, {1, 1, 1}};
int sharpKernel[KERNEL_SIZE][KERNEL_SIZE] = {{-1,-1,-1},{-1,9,-1},{-1,-1,-1}};
// Needed because out of the scope of this code
Image *image;
unsigned long n, m;

// structs
typedef struct {
   unsigned char red;
   unsigned char green;
   unsigned char blue;
} pixel;

typedef struct {
    int red;
    int green;
    int blue;
} pixel_sum;




// declarations
static pixel applyBlurKernel(int dim, int xPos, int yPos, pixel *src);
static pixel applyBlurKernelWithFilter(int dim, int xPos, int yPos, pixel *src);
static pixel applySharpenKernel(int dim, int xPos, int yPos, pixel *src);
void copyPixels(pixel* src, pixel* dst);

// implementations

/*
 * applyBlurKernel
 * Basically averages a 3x3 grid
 * Less branching
 * high locality
 * no loop since size of kernel is known
 * Removed unnecessary branches and loops
 */
 static pixel applyBlurKernel(int dim, int xPos, int yPos, pixel *src) {

        pixel_sum sum= {0};
        pixel current_pixel;
        pixel *pixelPointer;
        //initialize_pixel_sum(&sum);

      int firstRowStart = (xPos-1)*dim +  yPos-1;
      pixel pixels[9];
      pixelPointer = &src[firstRowStart];

      // save them close to each other - locality
      pixels[0] = *pixelPointer;
      pixels[1] = *(pixelPointer+1);
      pixels[2] = *(pixelPointer+2);
      pixels[3] = *(pixelPointer+dim);
      pixels[4]=  *(pixelPointer+dim+1);
      pixels[5]=  *(pixelPointer+dim+2);
      pixels[6]=  *(pixelPointer+2*dim);
      pixels[7]=  *(pixelPointer+2*dim+1);
      pixels[8]=  *(pixelPointer+2*dim+2);

      sum.red += (int) (pixels[0].red + pixels[1].red + pixels[2].red+ pixels[3].red+ pixels[4].red+ pixels[5].red+ pixels[6].red+ pixels[7].red+ pixels[8].red);
      sum.red /= 9;
      sum.green += (int) (pixels[0].green + pixels[1].green + pixels[2].green+ pixels[3].green+ pixels[4].green+ pixels[5].green+ pixels[6].green+ pixels[7].green+ pixels[8].green);
      sum.green /=9;
      sum.blue += (int) (pixels[0].blue + pixels[1].blue + pixels[2].blue+ pixels[3].blue+ pixels[4].blue+ pixels[5].blue+ pixels[6].blue+ pixels[7].blue+ pixels[8].blue);
      sum.blue /= 9;
      current_pixel.red =  sum.red;
      current_pixel.green =  sum.green;
      current_pixel.blue = sum.blue;
	return current_pixel;
}

/*
 * applyBlurKernelWithFilter
 * Basically averages 3x3 grid w/o the strongest and weakest pixel
 * Locality
 * Implemented a feature to detect if 3x3 is all white/black and do a "speed up" since finding min/max intensity could take time
 * Removed loops entirely from finding min/max intensity -> Not needed if 3x3 is known
 * ++var instead of var++
 * Removed unnecessary branches and loops
 */
 static pixel applyBlurKernelWithFilter(int dim, int xPos, int yPos, pixel *src) {

  pixel_sum sum= {0};
  pixel current_pixel;
  pixel *pixelPointer;

  int firstRowStart = (xPos-1)*dim + yPos-1;
  pixel pixels[9];
  int intensity[9] = {0,0,0,0,0,0,0,0,0};

  //locality
  pixelPointer = &src[firstRowStart];
  pixels[0] = *pixelPointer;
  pixels[1] = *(pixelPointer+1);
  pixels[2] = *(pixelPointer+2);
  pixels[3] = *(pixelPointer+dim);
  pixels[4]=  *(pixelPointer+dim+1);
  pixels[5]=  *(pixelPointer+dim+2);
  pixels[6]=  *(pixelPointer+2*dim);
  pixels[7]=  *(pixelPointer+2*dim+1);
  pixels[8]=  *(pixelPointer+2*dim+2);


  sum.red += (int) (pixels[0].red + pixels[1].red + pixels[2].red+ pixels[3].red+ pixels[4].red+ pixels[5].red+ pixels[6].red+ pixels[7].red+ pixels[8].red);
  intensity[0] +=pixels[0].red, intensity[1] +=pixels[1].red,intensity[2] +=pixels[2].red,intensity[3] +=pixels[3].red,intensity[4] +=pixels[4].red;
  intensity[5] +=pixels[5].red, intensity[6] +=pixels[6].red,intensity[7] +=pixels[7].red,intensity[8] += pixels[8].red;

  sum.green += (int) (pixels[0].green + pixels[1].green + pixels[2].green+ pixels[3].green+ pixels[4].green+ pixels[5].green+ pixels[6].green+ pixels[7].green+ pixels[8].green);
  intensity[0] +=pixels[0].green, intensity[1] +=pixels[1].green,intensity[2] +=pixels[2].green,intensity[3] +=pixels[3].green,intensity[4] +=pixels[4].green;
  intensity[5] +=pixels[5].green, intensity[6] +=pixels[6].green,intensity[7] +=pixels[7].green,intensity[8] += pixels[8].green;

  sum.blue += (int) (pixels[0].blue + pixels[1].blue + pixels[2].blue+ pixels[3].blue+ pixels[4].blue+ pixels[5].blue+ pixels[6].blue+ pixels[7].blue+ pixels[8].blue);
  intensity[0] +=pixels[0].blue, intensity[1] +=pixels[1].blue,intensity[2] +=pixels[2].blue,intensity[3] +=pixels[3].blue,intensity[4] +=pixels[4].blue;
  intensity[5] +=pixels[5].blue, intensity[6] +=pixels[6].blue,intensity[7] +=pixels[7].blue,intensity[8] += pixels[8].blue;

  // in case all white or black
  int sumSum = sum.blue + sum.green +sum.red;
  if (sumSum == 9*255*3) {
    current_pixel.red = 255;
    current_pixel.green = 255;
    current_pixel.blue = 255;
    return current_pixel;
  } else if (sumSum == 0) {
    current_pixel.red = 0;
    current_pixel.green = 0;
    current_pixel.blue = 0;
    return current_pixel;
  }
  // 0

  // find the max/min intensity
  int maxIntensity = intensity[0];
  int minIntensity = intensity[0];
  int maxIntensityIndex = 0;
  int minIntensityIndex =0;

  int *intensityPtr = &intensity[1];
  //1
  if (*intensityPtr > maxIntensity) {
    maxIntensity= *intensityPtr;
    maxIntensityIndex=1;
  } else if (*intensityPtr <= minIntensity) {
    minIntensity = *intensityPtr;
    minIntensityIndex=1;
  }
   ++intensityPtr;
  //2
  if (*intensityPtr > maxIntensity) {
    maxIntensity= *intensityPtr;
    maxIntensityIndex=2;
  } else if (*intensityPtr <= minIntensity) {
    minIntensity = *intensityPtr;
    minIntensityIndex=2;
  }
   ++intensityPtr;
  //3
  if (*intensityPtr > maxIntensity) {
    maxIntensity= *intensityPtr;
    maxIntensityIndex=3;
  } else if (*intensityPtr <= minIntensity) {
    minIntensity = *intensityPtr;
    minIntensityIndex=3;
  }
   ++intensityPtr;
  //4
  if (*intensityPtr > maxIntensity) {
    maxIntensity= *intensityPtr;
    maxIntensityIndex=4;
  } else if (*intensityPtr <= minIntensity) {
    minIntensity = *intensityPtr;
    minIntensityIndex=4;
  }
   ++intensityPtr;
  //5
  if (*intensityPtr > maxIntensity) {
    maxIntensity= *intensityPtr;
    maxIntensityIndex=5;
  } else if (*intensityPtr <= minIntensity) {
    minIntensity = *intensityPtr;
    minIntensityIndex=5;
  }
   ++intensityPtr;
  //6
  if (*intensityPtr > maxIntensity) {
    maxIntensity= *intensityPtr;
    maxIntensityIndex=6;
  } else if (*intensityPtr <= minIntensity) {
    minIntensity = *intensityPtr;
    minIntensityIndex=6;
  }
   ++intensityPtr;
  //7
  if (*intensityPtr > maxIntensity) {
    maxIntensity= *intensityPtr;
    maxIntensityIndex=7;
  } else if (*intensityPtr <= minIntensity) {
    minIntensity = *intensityPtr;
    minIntensityIndex=7;
  }
   ++intensityPtr;
  //8
  if (*intensityPtr > maxIntensity) {
    maxIntensity= *intensityPtr;
    maxIntensityIndex=8;
  } else if (*intensityPtr <= minIntensity) {
    minIntensity = *intensityPtr;
    minIntensityIndex=8;
  }

   current_pixel = pixels[minIntensityIndex];
    sum.red -= (int) current_pixel.red, sum.green -= (int) current_pixel.green, sum.blue -= (int) current_pixel.blue;
   current_pixel = pixels[maxIntensityIndex];
    sum.red-= (int) current_pixel.red, sum.green -= (int) current_pixel.green, sum.blue -= (int) current_pixel.blue;

    sum.red /= 7;
    sum.green /= 7;
    sum.blue /=7;

  current_pixel.red =  sum.red;
  current_pixel.green =  sum.green;
  current_pixel.blue =  sum.blue;
  return current_pixel;
}
/*
 * applySharpenKernel
 *
 * Locality
 * ++var instead of var++
 * Removed unnecessary branches and loops
 */
 static pixel applySharpenKernel(int dim, int xPos, int yPos, pixel *src) {

  pixel current_pixel;


  //initialize_pixel_sum(&sum);
  register int sumRed = 0, sumGreen=0, sumBlue=0;

  int startI = xPos-1;
  int startJ = yPos-1;
  int firstRowStart = startI*dim + startJ;

  // locality
  pixel currentPixel;
  currentPixel = src[firstRowStart];
  sumRed -=  currentPixel.red, sumGreen -= currentPixel.green, sumBlue -=  currentPixel.blue;
   ++firstRowStart;
  currentPixel = src[firstRowStart];
  sumRed -=  currentPixel.red, sumGreen -=  currentPixel.green, sumBlue -=  currentPixel.blue;
   ++firstRowStart;
  currentPixel = src[firstRowStart];
  sumRed -=  currentPixel.red, sumGreen-=  currentPixel.green, sumBlue -= currentPixel.blue;
   firstRowStart+= (dim-2);
  currentPixel = src[firstRowStart];
  sumRed -=  currentPixel.red, sumGreen -= currentPixel.green, sumBlue -=  currentPixel.blue;
   ++firstRowStart;
  currentPixel = src[firstRowStart];
  sumRed +=  9* currentPixel.red, sumGreen +=  9*  currentPixel.green, sumBlue += 9* currentPixel.blue;
   ++firstRowStart;
  currentPixel = src[firstRowStart];
  sumRed -=  currentPixel.red, sumGreen -= currentPixel.green, sumBlue -=  currentPixel.blue;
   firstRowStart+= (dim-2);
  currentPixel = src[firstRowStart];
  sumRed -=  currentPixel.red, sumGreen -=  currentPixel.green, sumBlue -=  currentPixel.blue;
   ++firstRowStart;
  currentPixel = src[firstRowStart];
  sumRed -=  currentPixel.red, sumGreen -=  currentPixel.green, sumBlue -= currentPixel.blue;
   ++firstRowStart;
  currentPixel = src[firstRowStart];
  sumRed -=  currentPixel.red, sumGreen -=  currentPixel.green, sumBlue -= currentPixel.blue;

  if(sumRed < 0) {
    sumRed =0;
  } else if (sumRed>= 256) {
    sumRed = 255;
  }
  if(sumGreen< 0) {
    sumGreen =0;
  } else if (sumGreen>= 256) {
    sumGreen = 255;
  }
  if(sumBlue < 0) {
    sumBlue =0;
  } else if (sumBlue>= 256) {
    sumBlue = 255;
  }

  current_pixel.red =  sumRed;
  current_pixel.green = sumGreen;
  current_pixel.blue =  sumBlue;
  return current_pixel;
}

/*
 * Smooth:
 * loop unrolling
 * Calling the specific function instead of letting the function itself check a clause for n*m times
 * Calc. multiplicities once
 * Reduced function arguments
 */
void smooth(int dim, pixel *src, pixel *dst, int kernel[KERNEL_SIZE][KERNEL_SIZE], bool filter) {

	int i, j;
    int maxRange = dim - 1;
    int carefulRange = maxRange-22;
    if (kernel == blurKernel) {
      if(filter) {
        for (i=1 ; i < maxRange; i++) {
          for (j =  1 ; j < carefulRange ; j+=20) {
            int epicNumber = i*dim+j;
            dst[epicNumber] = applyBlurKernelWithFilter(dim, i, j, src);
            dst[epicNumber+1] = applyBlurKernelWithFilter(dim, i, j+1, src);
            dst[epicNumber+2] = applyBlurKernelWithFilter(dim, i, j+2, src);
            dst[epicNumber+3] = applyBlurKernelWithFilter(dim, i, j+3, src);
            dst[epicNumber+4] = applyBlurKernelWithFilter(dim, i, j+4, src);
            dst[epicNumber+5] = applyBlurKernelWithFilter(dim, i, j+5, src);
            dst[epicNumber+6] = applyBlurKernelWithFilter(dim, i, j+6, src);
            dst[epicNumber+7] = applyBlurKernelWithFilter(dim, i, j+7, src);
            dst[epicNumber+8] = applyBlurKernelWithFilter(dim, i, j+8, src);
            dst[epicNumber+9] = applyBlurKernelWithFilter(dim, i, j+9, src);
            dst[epicNumber+10] = applyBlurKernelWithFilter(dim, i, j+10, src);
            dst[epicNumber+11] = applyBlurKernelWithFilter(dim, i, j+11, src);
            dst[epicNumber+12] = applyBlurKernelWithFilter(dim, i, j+12, src);
            dst[epicNumber+13] = applyBlurKernelWithFilter(dim, i, j+13, src);
            dst[epicNumber+14] = applyBlurKernelWithFilter(dim, i, j+14, src);
            dst[epicNumber+15] = applyBlurKernelWithFilter(dim, i, j+15, src);
            dst[epicNumber+16] = applyBlurKernelWithFilter(dim, i, j+16, src);
            dst[epicNumber+17] = applyBlurKernelWithFilter(dim, i, j+17, src);
            dst[epicNumber+18] = applyBlurKernelWithFilter(dim, i, j+18, src);
            dst[epicNumber+19] = applyBlurKernelWithFilter(dim, i, j+19, src);
          }
          for (; j < maxRange ; j++) {
            dst[i*dim+j] = applyBlurKernelWithFilter(dim, i, j, src);
          }
        }
      } else {
        for (i=1 ; i < maxRange; i++) {
          for (j =  1 ; j < carefulRange ; j+=20) {
            int epicNumber = i*dim+j;
            dst[epicNumber] = applyBlurKernel(dim, i, j, src);
            dst[epicNumber+1] = applyBlurKernel(dim, i, j+1, src);
            dst[epicNumber+2] = applyBlurKernel(dim, i, j+2, src);
            dst[epicNumber+3] = applyBlurKernel(dim, i, j+3, src);
            dst[epicNumber+4] = applyBlurKernel(dim, i, j+4, src);
            dst[epicNumber+5] = applyBlurKernel(dim, i, j+5, src);
            dst[epicNumber+6] = applyBlurKernel(dim, i, j+6, src);
            dst[epicNumber+7] = applyBlurKernel(dim, i, j+7, src);
            dst[epicNumber+8] = applyBlurKernel(dim, i, j+8, src);
            dst[epicNumber+9] = applyBlurKernel(dim, i, j+9, src);
            dst[epicNumber+10] = applyBlurKernel(dim, i, j+10, src);
            dst[epicNumber+11] = applyBlurKernel(dim, i, j+11, src);
            dst[epicNumber+12] = applyBlurKernel(dim, i, j+12, src);
            dst[epicNumber+13] = applyBlurKernel(dim, i, j+13, src);
            dst[epicNumber+14] = applyBlurKernel(dim, i, j+14, src);
            dst[epicNumber+15] = applyBlurKernel(dim, i, j+15, src);
            dst[epicNumber+16] = applyBlurKernel(dim, i, j+16, src);
            dst[epicNumber+17] = applyBlurKernel(dim, i, j+17, src);
            dst[epicNumber+18] = applyBlurKernel(dim, i, j+18, src);
            dst[epicNumber+19] = applyBlurKernel(dim, i, j+19, src);
          }
          for (; j < maxRange ; j++) {
            dst[i*dim+j] = applyBlurKernel(dim, i, j, src);
          }
        }
      }

    } else {
      for (i=1 ; i < maxRange; i++) {
        for (j =  1 ; j < carefulRange ; j+=20) {
          int epicNumber = i*dim+j;
          dst[epicNumber] = applySharpenKernel(dim, i, j, src);
          dst[epicNumber+1] = applySharpenKernel(dim, i, j+1, src);
          dst[epicNumber+2] = applySharpenKernel(dim, i, j+2, src);
          dst[epicNumber+3] = applySharpenKernel(dim, i, j+3, src);
          dst[epicNumber+4] = applySharpenKernel(dim, i, j+4, src);
          dst[epicNumber+5] = applySharpenKernel(dim, i, j+5, src);
          dst[epicNumber+6] = applySharpenKernel(dim, i, j+6, src);
          dst[epicNumber+7] = applySharpenKernel(dim, i, j+7, src);
          dst[epicNumber+8] = applySharpenKernel(dim, i, j+8, src);
          dst[epicNumber+9] = applySharpenKernel(dim, i, j+9, src);
          dst[epicNumber+10] = applySharpenKernel(dim, i, j+10, src);
          dst[epicNumber+11] = applySharpenKernel(dim, i, j+11, src);
          dst[epicNumber+12] = applySharpenKernel(dim, i, j+12, src);
          dst[epicNumber+13] = applySharpenKernel(dim, i, j+13, src);
          dst[epicNumber+14] = applySharpenKernel(dim, i, j+14, src);
          dst[epicNumber+15] = applySharpenKernel(dim, i, j+15, src);
          dst[epicNumber+16] = applySharpenKernel(dim, i, j+16, src);
          dst[epicNumber+17] = applySharpenKernel(dim, i, j+17, src);
          dst[epicNumber+18] = applySharpenKernel(dim, i, j+18, src);
          dst[epicNumber+19] = applySharpenKernel(dim, i, j+19, src);
        }
        for (; j < maxRange ; j++) {
          dst[i*dim+j] = applySharpenKernel(dim, i, j, src);
        }
      }
    }

}


// Both chars to pixels and pixelsToChars are just glorified memory copy so they use my copyPixels implementation w/ casting
/*
 * charsToPixel
 * based on copyPixels (kinda neat right?)
 */
void charsToPixels(Image *charsImg, pixel* pixels) {
  void *destStart =pixels;
  void *sourceStart = &image->data[0];
  copyPixels(sourceStart,destStart);
}
/*
 * pixelsToChars
 * based on copyPixels ;)
 */
void pixelsToChars(pixel* pixels, Image *charsImg) {
    void *destStart = (void*) &image->data[0];
    void *sourceStart = pixels;
    copyPixels(sourceStart, destStart);
}

/*
 * CopyPixels
 * Loop unrolling
 * Copying using unsigned long instead of char
 * ++var instead of var++
 * pointers instead of array access
 *
 */
void copyPixels(pixel* src, pixel* dst) {
    // the start which we change
    void* start = (void*) src;
    // the destination image
    void* destination = (void*) dst;
    // the end which we do not change (non-inclusive) of the source
    void* end = src + n*m;
    unsigned long* longEnd = (unsigned long *) end;
    char* shortEnd = (char *) end;


    unsigned long *longSourceIndex = (unsigned long*) start;
    unsigned long *longDestinationIndex = (unsigned long*) destination;

    while (longSourceIndex + 12 < longEnd) {
      // 1
      *longDestinationIndex = *longSourceIndex;
      ++longSourceIndex;
      ++longDestinationIndex;
      // 2
      *longDestinationIndex = *longSourceIndex;
      ++longSourceIndex;
      ++longDestinationIndex;
      // 3
      *longDestinationIndex = *longSourceIndex;
      ++longSourceIndex;
      ++longDestinationIndex;
      // 4
      *longDestinationIndex = *longSourceIndex;
      ++longSourceIndex;
      ++longDestinationIndex;
      // 5
      *longDestinationIndex = *longSourceIndex;
      ++longSourceIndex;
      ++longDestinationIndex;
      // 6
      *longDestinationIndex = *longSourceIndex;
      ++longSourceIndex;
      ++longDestinationIndex;
      // 7
      *longDestinationIndex = *longSourceIndex;
      ++longSourceIndex;
      ++longDestinationIndex;
      // 8
      *longDestinationIndex = *longSourceIndex;
      ++longSourceIndex;
      ++longDestinationIndex;
      // 9
      *longDestinationIndex = *longSourceIndex;
      ++longSourceIndex;
      ++longDestinationIndex;
      // 10
      *longDestinationIndex = *longSourceIndex;
      ++longSourceIndex;
      ++longDestinationIndex;
    }
    char *shortSourceIndex = (char *) longSourceIndex;
    char *shortDestinationIndex = (char *) longDestinationIndex;
    while (shortSourceIndex < shortEnd) {
      *shortDestinationIndex = *shortSourceIndex;
      ++shortSourceIndex;
      ++shortDestinationIndex;
    }
}

/*
 * doConvolution
 * Fewer Arguments
 * only runs a few times, won't produce a bottleneck
 */
void doConvolution(Image *image, int kernel[KERNEL_SIZE][KERNEL_SIZE], int kernelScale, bool filter) {

	pixel* pixelsImg = malloc(m*n*sizeof(pixel));
	pixel* backupOrg = malloc(m*n*sizeof(pixel));

	charsToPixels(image, pixelsImg);
	copyPixels(pixelsImg, backupOrg);
	smooth(m, backupOrg, pixelsImg, kernel, filter);

	pixelsToChars(pixelsImg, image);

	free(pixelsImg);
	free(backupOrg);
}

/*
 * myfunction
 * The "main" function here
 * Fewer arguments to called functions
 */
void myfunction(Image *image, char* srcImgpName, char* blurRsltImgName, char* sharpRsltImgName, char* filteredBlurRsltImgName, char* filteredSharpRsltImgName, char flag) {
      if (flag == '1') {
        // blur image
        doConvolution(image, blurKernel, 9, false);

        // write result image to file
        writeBMP(image, srcImgpName, blurRsltImgName);

        // sharpen the resulting image
        doConvolution(image, sharpKernel, 1, false);

        // write result image to file
        writeBMP(image, srcImgpName, sharpRsltImgName);
      } else {
        // apply extermum filtered kernel to blur image
        doConvolution(image, blurKernel, 7, true);

        // write result image to file
        writeBMP(image, srcImgpName, filteredBlurRsltImgName);

        // sharpen the resulting image
        doConvolution(image, sharpKernel, 1, false);

        // write result image to file
        writeBMP(image, srcImgpName, filteredSharpRsltImgName);
      }

}

