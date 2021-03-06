#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include "image.h"
#include "matrix.h"
#include <time.h>

// Frees an array of descriptors.
// descriptor *d: the array.
// int n: number of elements in array.
void free_descriptors(descriptor *d, int n)
{
    int i;
    for(i = 0; i < n; ++i){
        free(d[i].data);
    }
    free(d);
}

// Create a feature descriptor for an index in an image.
// image im: source image.
// int i: index in image for the pixel we want to describe.
// returns: descriptor for that index.
descriptor describe_index(image im, int i)
{
    int w = 5;
    descriptor d;
    d.p.x = i%im.w;
    d.p.y = i/im.w;
    d.data = calloc(w*w*im.c, sizeof(float));
    d.n = w*w*im.c;
    int c, dx, dy;
    int count = 0;
    // If you want you can experiment with other descriptors
    // This subtracts the central value from neighbors
    // to compensate some for exposure/lighting changes.
    for(c = 0; c < im.c; ++c){
        float cval = im.data[c*im.w*im.h + i];
        for(dx = -w/2; dx < (w+1)/2; ++dx){
            for(dy = -w/2; dy < (w+1)/2; ++dy){
                float val = get_pixel(im, i%im.w+dx, i/im.w+dy, c);
                d.data[count++] = cval - val;
            }
        }
    }
    return d;
}

// Marks the spot of a point in an image.
// image im: image to mark.
// ponit p: spot to mark in the image.
void mark_spot(image im, point p)
{
    int x = p.x;
    int y = p.y;
    int i;
    for(i = -9; i < 10; ++i){
        set_pixel(im, x+i, y, 0, 1);
        set_pixel(im, x, y+i, 0, 1);
        set_pixel(im, x+i, y, 1, 0);
        set_pixel(im, x, y+i, 1, 0);
        set_pixel(im, x+i, y, 2, 1);
        set_pixel(im, x, y+i, 2, 1);
    }
}

// Marks corners denoted by an array of descriptors.
// image im: image to mark.
// descriptor *d: corners in the image.
// int n: number of descriptors to mark.
void mark_corners(image im, descriptor *d, int n)
{
    int i;
    for(i = 0; i < n; ++i){
        mark_spot(im, d[i].p);
    }
}

// Creates a 1d Gaussian filter.
// float sigma: standard deviation of Gaussian.
// returns: single row image of the filter.
image make_1d_gaussian(float sigma)
{
	// optional, make separable 1d Gaussian.

	int size = (int) ceil(6*sigma) % 2 == 0 ? ceil(6*sigma) + 1 : ceil(6*sigma);
	image result = make_image(size, 1,1);
	float k = 1.0f/(sqrtf(TWOPI)*sigma);

	for (int i = 0; i < size; ++i) {
		int x = i - (size - 1.0f)/2.0f;
		float z = -1.0f*(x*x)/(2 * sigma * sigma);
		set_pixel(result, i, 0, 0, k * expf(z));
	}

	//l1_normalize(result);

	return result;
}

image make_kernel_transpose(image im)
{
	image result = make_image(im.h, im.w, im.c);

	for (int i = 0; i < result.w; ++i) {
		for (int j = 0; j < result.h; ++j) {
			for (int k = 0; k < result.c; ++k) {
				set_pixel(result, i, j, k, get_pixel(im, j, i, k));
			}
		}
	}

	return result;
}

// Smooths an image using separable Gaussian filter.
// image im: image to smooth.
// float sigma: std dev. for Gaussian.
// returns: smoothed image.
image smooth_image(image im, float sigma)
{
    if(0){
        image g = make_gaussian_filter(sigma);
        image s = convolve_image(im, g, 1);
        free_image(g);
        return s;
    } else {
        // optional, use two convolutions with 1d gaussian filter.
        // If you implement, disable the above if check.

	    image g1 = make_1d_gaussian(sigma);
	    image g2 = make_kernel_transpose(g1);

	    image s = convolve_image(im, g1, 1);
	    image s1 = convolve_image(s, g2, 1);
	    free_image(g1);
	    free_image(g2);
	    free_image(s);

	    return s1;
    }
}

void insert_channel(image im, image insertion, int channel) {
	assert(channel < im.c);
	assert(insertion.c == 1);

	memcpy(im.data + im.w * im.h * channel, insertion.data, im.w * im.h * sizeof(float));
}

// Calculate the structure matrix of an image.
// image im: the input image.
// float sigma: std dev. to use for weighted sum.
// returns: structure matrix. 1st channel is Ix^2, 2nd channel is Iy^2,
//          third channel is IxIy.
image structure_matrix(image im, float sigma)
{
    image S = make_image(im.w, im.h, 3);

	image gxFilter = make_gx_filter();
	image Ix = convolve_image(im, gxFilter, 0);
	image Ix_2 = mult_image(Ix, Ix);
	insert_channel(S, Ix_2, 0);
	free_image(gxFilter);
	free_image(Ix_2);

	image gyFilter = make_gy_filter();
	image Iy = convolve_image(im, gyFilter, 0);
	image Iy_2 = mult_image(Iy, Iy);
	insert_channel(S, Iy_2, 1);
	free_image(gyFilter);
	free_image(Iy_2);

	image IxIy = mult_image(Ix, Iy);
	insert_channel(S, IxIy, 2);
	free_image(Ix);
	free_image(Iy);
	free_image(IxIy);

	image result = smooth_image(S, sigma);

    return result;
}

// Estimate the cornerness of each pixel given a structure matrix S.
// image S: structure matrix for an image.
// returns: a response map of cornerness calculations.
image cornerness_response(image S)
{
    image R = make_image(S.w, S.h, 1);
    // fill in R, "cornerness" for each pixel using the structure matrix.
    // We'll use formulation det(S) - alpha * trace(S)^2, alpha = .06.

    float alpha = 0.06f;

	for (int i = 0; i < R.w; ++i) {
		for (int j = 0; j < R.h; ++j) {
			float a11 = get_pixel(S, i, j, 0);
			float a12 = get_pixel(S, i, j, 2);
			float a21 = get_pixel(S, i, j, 2);
			float a22 = get_pixel(S, i, j, 1);

			float det = a11 * a22 - a12 * a21;
			float trace = a11 + a22;
			float cornerness = det - alpha * trace * trace;

			set_pixel(R, i, j, 0, cornerness);
		}
	}

    return R;
}

// Perform non-max supression on an image of feature responses.
// image im: 1-channel image of feature responses.
// int w: distance to look for larger responses.
// returns: image with only local-maxima responses within w pixels.
image nms_image(image im, int w)
{
    image r = copy_image(im);
    // perform NMS on the response map.
    // for every pixel in the image:
    //     for neighbors within w:
    //         if neighbor response greater than pixel response:
    //             set response to be very low (I use -999999 [why not 0??])

	for (int i = 0; i < im.w; ++i) {
		for (int j = 0; j < im.h; ++j) {
			float currentResponse = get_pixel(im, i, j, 0);

			for (int k = i - w ; k < i + w; ++k) {
				for (int l = j - w; l < j + w; ++l) {
					if(get_pixel(im, k, l, 0) > currentResponse) {
						set_pixel(r, i, j, 0, -INFINITY);
						goto nextPixel;
					}
				}
			}

			nextPixel:
			continue;
		}
	}
    return r;
}

// Perform harris corner detection and extract features from the corners.
// image im: input image.
// float sigma: std. dev for harris.
// float thresh: threshold for cornerness.
// int nms: distance to look for local-maxes in response map.
// int *n: pointer to number of corners detected, should fill in.
// returns: array of descriptors of the corners in the image.
descriptor *harris_corner_detector(image im, float sigma, float thresh, int nms, int *n)
{
    // Calculate structure matrix
    image S = structure_matrix(im, sigma);

    // Estimate cornerness
    image R = cornerness_response(S);

    // Run NMS on the responses
    image Rnms = nms_image(R, nms);


    //count number of responses over threshold
    int count = 0; // change this

	for (int i = 0; i < Rnms.w; ++i) {
		for (int j = 0; j < Rnms.h; ++j) {
			if(get_pixel(Rnms, i, j, 0) > thresh) {
				count++;
			}
		}
	}

    *n = count; // <- set *n equal to number of corners in image.
    descriptor *d = calloc(count, sizeof(descriptor));

    //fill in array *d with descriptors of corners, use describe_index().
	int descriptorIndex = 0;
	for (int i = 0; i < Rnms.w; ++i) {
		for (int j = 0; j < Rnms.h; ++j) {
			if(get_pixel(Rnms, i, j, 0) > thresh) {
				d[descriptorIndex++] = describe_index(im, i + j * im.w);
			}
		}
	}

    free_image(S);
    free_image(R);
    free_image(Rnms);
    return d;
}

// Find and draw corners on an image.
// image im: input image.
// float sigma: std. dev for harris.
// float thresh: threshold for cornerness.
// int nms: distance to look for local-maxes in response map.
void detect_and_draw_corners(image im, float sigma, float thresh, int nms)
{
    int n = 0;
    descriptor *d = harris_corner_detector(im, sigma, thresh, nms, &n);
    mark_corners(im, d, n);
}
