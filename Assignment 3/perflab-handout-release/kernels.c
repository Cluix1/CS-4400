/*******************************************
 * Solutions for the CS:APP Performance Lab
 ********************************************/

#include <stdio.h>
#include <stdlib.h>
#include "defs.h"

/* 
 * Please fill in the following student struct 
 */
student_t student = {
  "Harry Q. Bovik",     /* Full name */
  "no_one@nowhere.edu",  /* Email address */
};

/***************
 * COMPLEX KERNEL
 ***************/

/******************************************************
 * Your different versions of the complex kernel go here
 ******************************************************/

/* 
 * naive_complex - The naive baseline version of complex 
 */
char naive_complex_descr[] = "naive_complex: Naive baseline implementation";
void naive_complex(int dim, pixel *src, pixel *dest)
{
  int i, j;

  for(i = 0; i < dim; i++)
    for(j = 0; j < dim; j++)
    {

      dest[RIDX(dim - j - 1, dim - i - 1, dim)].red = ((int)src[RIDX(i, j, dim)].red +
						      (int)src[RIDX(i, j, dim)].green +
						      (int)src[RIDX(i, j, dim)].blue) / 3;
      
      dest[RIDX(dim - j - 1, dim - i - 1, dim)].green = ((int)src[RIDX(i, j, dim)].red +
							(int)src[RIDX(i, j, dim)].green +
							(int)src[RIDX(i, j, dim)].blue) / 3;
      
      dest[RIDX(dim - j - 1, dim - i - 1, dim)].blue = ((int)src[RIDX(i, j, dim)].red +
						       (int)src[RIDX(i, j, dim)].green +
						       (int)src[RIDX(i, j, dim)].blue) / 3;

    }
}


/* 
 * complex - Your current working version of complex
 * IMPORTANT: This is the version you will be graded on
 */
char complex_descr[] = "complex: 32x32 blocked rotate, mirror, and grayscale";
void complex(int dim, pixel *src, pixel *dest)
{
  int ii, jj, i, j;

  /* Blocking keeps both the source rows and destination rows in cache. */
  for (ii = 0; ii < dim; ii += 32)
    for (jj = 0; jj < dim; jj += 32)
      for (i = ii; i < ii + 32; i++) {
        pixel *s = src + RIDX(i, jj, dim);
        pixel *d = dest + RIDX(dim - jj - 1, dim - i - 1, dim);

        for (j = 0; j < 32; j++) {
          unsigned short gray = (unsigned short)
            (((int)s[j].red + (int)s[j].green + (int)s[j].blue) / 3);
          d->red = gray;
          d->green = gray;
          d->blue = gray;
          d -= dim;
        }
      }
}

/*********************************************************************
 * register_complex_functions - Register all of your different versions
 *     of the complex kernel with the driver by calling the
 *     add_complex_function() for each test function. When you run the
 *     driver program, it will test and report the performance of each
 *     registered test function.  
 *********************************************************************/

void register_complex_functions() {
  add_complex_function(&complex, complex_descr);
  add_complex_function(&naive_complex, naive_complex_descr);
}


/***************
 * MOTION KERNEL
 **************/

/***************************************************************
 * Various helper functions for the motion kernel
 * You may modify these or add new ones any way you like.
 **************************************************************/


/* 
 * weighted_combo - Returns new pixel value at (i,j) 
 */
static pixel weighted_combo(int dim, int i, int j, pixel *src) 
{
  int ii, jj;
  pixel current_pixel;

  int red, green, blue;
  red = green = blue = 0;

  int num_neighbors = 0;
  for(ii=0; ii < 3; ii++)
    for(jj=0; jj < 3; jj++) 
      if ((i + ii < dim) && (j + jj < dim)) 
      {
	num_neighbors++;
	red += (int) src[RIDX(i+ii,j+jj,dim)].red;
	green += (int) src[RIDX(i+ii,j+jj,dim)].green;
	blue += (int) src[RIDX(i+ii,j+jj,dim)].blue;
      }
  
  current_pixel.red = (unsigned short) (red / num_neighbors);
  current_pixel.green = (unsigned short) (green / num_neighbors);
  current_pixel.blue = (unsigned short) (blue / num_neighbors);
  
  return current_pixel;
}



/******************************************************
 * Your different versions of the motion kernel go here
 ******************************************************/


/*
 * naive_motion - The naive baseline version of motion 
 */
char naive_motion_descr[] = "naive_motion: Naive baseline implementation";
void naive_motion(int dim, pixel *src, pixel *dst) 
{
  int i, j;
    
  for (i = 0; i < dim; i++)
    for (j = 0; j < dim; j++)
      dst[RIDX(i, j, dim)] = weighted_combo(dim, i, j, src);
}


/*
 * motion - Your current working version of motion. 
 * IMPORTANT: This is the version you will be graded on
 */
char motion_descr[] = "motion: sliding 3x3 component sums";
void motion(int dim, pixel *src, pixel *dst) 
{
  int i, j;

  /* The common case has a complete 3 by 3 block.  Keep its running
     component sums and replace one column as the block moves right. */
  for (i = 0; i < dim - 2; i++) {
    pixel *r0 = src + RIDX(i, 0, dim);
    pixel *r1 = r0 + dim;
    pixel *r2 = r1 + dim;
    pixel *d = dst + RIDX(i, 0, dim);
    int red = r0[0].red + r0[1].red + r0[2].red +
              r1[0].red + r1[1].red + r1[2].red +
              r2[0].red + r2[1].red + r2[2].red;
    int green = r0[0].green + r0[1].green + r0[2].green +
                r1[0].green + r1[1].green + r1[2].green +
                r2[0].green + r2[1].green + r2[2].green;
    int blue = r0[0].blue + r0[1].blue + r0[2].blue +
               r1[0].blue + r1[1].blue + r1[2].blue +
               r2[0].blue + r2[1].blue + r2[2].blue;

    for (j = 0; j < dim - 2; j++) {
      d[j].red = (unsigned short)(red / 9);
      d[j].green = (unsigned short)(green / 9);
      d[j].blue = (unsigned short)(blue / 9);

      if (j < dim - 3) {
        red += r0[j + 3].red + r1[j + 3].red + r2[j + 3].red
             - r0[j].red - r1[j].red - r2[j].red;
        green += r0[j + 3].green + r1[j + 3].green + r2[j + 3].green
               - r0[j].green - r1[j].green - r2[j].green;
        blue += r0[j + 3].blue + r1[j + 3].blue + r2[j + 3].blue
              - r0[j].blue - r1[j].blue - r2[j].blue;
      }
    }

    /* The final two columns have smaller neighborhoods. */
    red = r0[dim - 2].red + r0[dim - 1].red +
          r1[dim - 2].red + r1[dim - 1].red +
          r2[dim - 2].red + r2[dim - 1].red;
    green = r0[dim - 2].green + r0[dim - 1].green +
            r1[dim - 2].green + r1[dim - 1].green +
            r2[dim - 2].green + r2[dim - 1].green;
    blue = r0[dim - 2].blue + r0[dim - 1].blue +
           r1[dim - 2].blue + r1[dim - 1].blue +
           r2[dim - 2].blue + r2[dim - 1].blue;
    d[dim - 2].red = (unsigned short)(red / 6);
    d[dim - 2].green = (unsigned short)(green / 6);
    d[dim - 2].blue = (unsigned short)(blue / 6);

    red = r0[dim - 1].red + r1[dim - 1].red + r2[dim - 1].red;
    green = r0[dim - 1].green + r1[dim - 1].green + r2[dim - 1].green;
    blue = r0[dim - 1].blue + r1[dim - 1].blue + r2[dim - 1].blue;
    d[dim - 1].red = (unsigned short)(red / 3);
    d[dim - 1].green = (unsigned short)(green / 3);
    d[dim - 1].blue = (unsigned short)(blue / 3);
  }

  /* Bottom two rows: their blocks are only two and one rows high. */
  i = dim - 2;
  for (j = 0; j < dim - 2; j++) {
    int red = src[RIDX(i, j, dim)].red + src[RIDX(i, j + 1, dim)].red + src[RIDX(i, j + 2, dim)].red +
              src[RIDX(i + 1, j, dim)].red + src[RIDX(i + 1, j + 1, dim)].red + src[RIDX(i + 1, j + 2, dim)].red;
    int green = src[RIDX(i, j, dim)].green + src[RIDX(i, j + 1, dim)].green + src[RIDX(i, j + 2, dim)].green +
                src[RIDX(i + 1, j, dim)].green + src[RIDX(i + 1, j + 1, dim)].green + src[RIDX(i + 1, j + 2, dim)].green;
    int blue = src[RIDX(i, j, dim)].blue + src[RIDX(i, j + 1, dim)].blue + src[RIDX(i, j + 2, dim)].blue +
               src[RIDX(i + 1, j, dim)].blue + src[RIDX(i + 1, j + 1, dim)].blue + src[RIDX(i + 1, j + 2, dim)].blue;
    dst[RIDX(i, j, dim)].red = (unsigned short)(red / 6);
    dst[RIDX(i, j, dim)].green = (unsigned short)(green / 6);
    dst[RIDX(i, j, dim)].blue = (unsigned short)(blue / 6);
  }

  for (j = dim - 2; j < dim; j++) {
    int width = dim - j;
    int red = 0, green = 0, blue = 0;
    int ii, jj;
    for (ii = dim - 2; ii < dim; ii++)
      for (jj = j; jj < dim; jj++) {
        red += src[RIDX(ii, jj, dim)].red;
        green += src[RIDX(ii, jj, dim)].green;
        blue += src[RIDX(ii, jj, dim)].blue;
      }
    dst[RIDX(dim - 2, j, dim)].red = (unsigned short)(red / (2 * width));
    dst[RIDX(dim - 2, j, dim)].green = (unsigned short)(green / (2 * width));
    dst[RIDX(dim - 2, j, dim)].blue = (unsigned short)(blue / (2 * width));
  }

  i = dim - 1;
  for (j = 0; j < dim - 2; j++) {
    int red = src[RIDX(i, j, dim)].red + src[RIDX(i, j + 1, dim)].red + src[RIDX(i, j + 2, dim)].red;
    int green = src[RIDX(i, j, dim)].green + src[RIDX(i, j + 1, dim)].green + src[RIDX(i, j + 2, dim)].green;
    int blue = src[RIDX(i, j, dim)].blue + src[RIDX(i, j + 1, dim)].blue + src[RIDX(i, j + 2, dim)].blue;
    dst[RIDX(i, j, dim)].red = (unsigned short)(red / 3);
    dst[RIDX(i, j, dim)].green = (unsigned short)(green / 3);
    dst[RIDX(i, j, dim)].blue = (unsigned short)(blue / 3);
  }

  for (j = dim - 2; j < dim; j++) {
    int width = dim - j;
    int red = 0, green = 0, blue = 0;
    int jj;
    for (jj = j; jj < dim; jj++) {
      red += src[RIDX(dim - 1, jj, dim)].red;
      green += src[RIDX(dim - 1, jj, dim)].green;
      blue += src[RIDX(dim - 1, jj, dim)].blue;
    }
    dst[RIDX(dim - 1, j, dim)].red = (unsigned short)(red / width);
    dst[RIDX(dim - 1, j, dim)].green = (unsigned short)(green / width);
    dst[RIDX(dim - 1, j, dim)].blue = (unsigned short)(blue / width);
  }
}

/********************************************************************* 
 * register_motion_functions - Register all of your different versions
 *     of the motion kernel with the driver by calling the
 *     add_motion_function() for each test function.  When you run the
 *     driver program, it will test and report the performance of each
 *     registered test function.  
 *********************************************************************/

void register_motion_functions() {
  add_motion_function(&motion, motion_descr);
  add_motion_function(&naive_motion, naive_motion_descr);
}
