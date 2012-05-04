#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>

#include "tadbit.h"


double ml_ab(double *k, double *d, double *ab, int n) {
/*
   * The 2-array 'ab' is upated in place and the log-likelihood
   * is returned.
   * The fitted model (by maximum likelihood) is Poisson with lambda
   * paramter such that lambda = exp(a + b*d). So the full log-likelihood
   * of the model is Sigma -exp(a + b*d_i) + k_i(a + b*d_i).
*/

   if (n < 1) {
      return 0.0;
   }
   if (n < 3) {
      return NAN;
   }

   int i, j = 0;
   double a = ab[0], b = ab[1], llik;
   double da = 0.0, db = 0.0, oldgrad;
   double f, g, dfda = 0.0, dfdb = 0.0, dgda = 0.0, dgdb = 0.0;
   double denom;

   // 'tmp' is a computation intermediate that will be the return
   // value of 'exp'. This can call '__slowexp' which on 64-bit machines
   // can return a long double (causing segmentation fault if 'tmp' is
   // declared as long).
   long double tmp;

   // Comodity function.
   void recompute_fg() {
      f = 0.0; g = 0.0;
      for (i = 0 ; i < n ; i++) {
         tmp  =  exp(a+da+(b+db)*d[i])-k[i];
         f   +=  tmp;
         g   +=  tmp * d[i];
      }
   }

   recompute_fg();

   // Newton-Raphson until gradient function < TOLERANCE.
   while ((oldgrad = f*f + g*g) > TOLERANCE) {

      // Compute the derivatives.
      dfda = dfdb = dgda = dgdb = 0.0;
      for (i = 0 ; i < n ; i++) {
         tmp   =   exp(a+b*d[i]);
         dfda +=   tmp;
         dgda +=   tmp * d[i];
         dgdb +=   tmp * d[i]*d[i];
      }
      dfdb = dgda;

      denom = dfdb*dgda - dfda*dgdb;
      da = (f*dgdb - g*dfdb) / denom;
      db = (g*dfda - f*dgda) / denom;

      recompute_fg();
      // Gradient test. Traceback if not going down the gradient.
      while (f*f + g*g > oldgrad) {
         da /= 2;
         db /= 2;
         recompute_fg();
      }

      // Update 'a' and 'b'.
      a += da;
      b += db;


   }

   // Compute log-likelihood (using 'dfda').
   llik = 0.0;
   for (i = 0 ; i < n ; i++) {
      llik += exp(a+b*d[i]) + k[i] * (a + b*d[i]);
   }

   // Update 'ab' in place (to make the estimates available).
   ab[0] = a; ab[1] = b;

   return llik;

}

void slice(double *k, double *d, int n, int i, int j, ml_blocks *blocks) {
/*
   *  Break up 'mat' in three blocks delimited by 'i' and 'j'.
   *  The upper block is (0,i-1)x(i,j), the triangular block is
   *  the upper triangular block without diagonal (i,j)x(i,j)
   *  and the bottom block is (j+1,n)x(i,j).
*/

   int l, row, col;
   int top_counter = 0, tri_counter = 0, bot_counter = 0;

   for (l = 0 ; l < 3 ; l++) {
      blocks->size[l] = 0;
   }

   // Fill vertically.
   for (col = i ; col < j+1 ; col++) {
      // Skip if 'i' is 0.
      for (row = 0 ; row < i ; row++) {
         if (!isnan(k[row+col*n])) {
            blocks->k[0][blocks->size[0]] = k[row+col*n];
            blocks->d[0][blocks->size[0]] = d[row+col*n];
            blocks->size[0]++;
         }
      }

      // Skip if 'col' is i.
      for (row = i ; row < col ; row++) {
         if (!isnan(k[row+col*n])) {
            blocks->k[1][blocks->size[1]] = k[row+col*n];
            blocks->d[1][blocks->size[1]] = d[row+col*n];
            blocks->size[1]++;
         }
      }

      // Skip if 'j' is n-1.
      for (row = j+1 ; row < n ; row++) {
         if (!isnan(k[row+col*n])) {
            blocks->k[2][blocks->size[2]] = k[row+col*n];
            blocks->d[2][blocks->size[2]] = d[row+col*n];
            blocks->size[2]++;
         }
      }
   }
}

double quantile (double array[], int n, double quant) {

   if (quant < 0.0 || quant > 1.0) {
      return NAN;
   }

   // Comparison function.
   int comp(const void * a, const void * b) {
      return ( *(double*)a - *(double*)b );
   }

   // Copy array (while removin NAs).
   int i, j = 0;
   double sorted_array[n];
   for (i = 0 ; i < n ; i++) {
      if (!isnan(array[i])) {
         sorted_array[j++] = array[i];
      }
   }

   if (j < 1) {
      return NAN;
   }
   
   // Sort and return the quant-th position.
   qsort(sorted_array, n, sizeof(double), comp);
   return sorted_array[(int) (quant*j)];

}


void remove_non_local_maxima (double **obs, double *dis, int n, int m,
    ml_blocks *blocks, int *bkpts) {
/*
*/

   int i, j, k, no_real_value;
   double tmp, dist[m][n];

   // Compute distances asd absolute differences.
   for (k = 0 ; k < m ; k++) {
      for (j = 0 ; j < n-1 ; j++) {
         // Initialize with a negative value. Set distance to NA
         // if it is still negative after additions.
         dist[k][j] = 0.0;
         no_real_value = 1;
         for (i = 0 ; i < n ; i++) {
            // Do not compute the difference with diagonal terms.
            if (i == j || i == j+1) {
               continue;
            }
            tmp = abs(obs[k][i+j*n] - obs[k][i+(j+1)*n]);
            if (!isnan(tmp)) {
               no_real_value = 0;
               dist[k][j] += tmp;
            }
         }
         if (no_real_value) {
            dist[k][j] = NAN;
         }
      }
   }


   // Initialize.
   bkpts[n-1] = 1;
   for (i = 0 ; i < n-1 ; i++) {
      bkpts[i] = 0;
   }

   double decile;
   // Set a potential breakpoint if distance is in top 10%.
   for (k = 0 ; k < m ; k++) {
      decile = quantile(dist[k], n, 0.9);
      for (i = 3 ; i < n-1 ; i++) {
         if (dist[k][i] > decile) {
            bkpts[i] = 1;
         }
      }
   }

}

int *get_breakpoints(double *llik, int n, int *all_breakpoints) {
/*
   * Find the maximum likelihood estimate by a dynamic
   * programming algorithm.
*/

   int i,j, nbreaks = 0;
   int new_breakpoint = 0;

   double tmp;

   double new_llik[n];
   double old_llik[n];
   // Initialize to first line of 'llik'.
   for (j = 0 ; j < n ; j++) {
      old_llik[j] = llik[j*n];
   }

   // Breakpoint lists. The first index is the end of the segment,
   // the second is 1 if this position is an end (breakpoint).
   
   // int new_bkpt_list[n][n];
   // int old_bkpt_list[n][n];
   int *new_bkpt_list = (int *) malloc(n*n * sizeof(int));
   int *old_bkpt_list = (int *) malloc(n*n * sizeof(int));

   // Initialize to 0.
   for (i = 0 ; i < n*n ; i++) {
      new_bkpt_list[i] = old_bkpt_list[i] = 0;
   }

   double new_full_llik = old_llik[n-1];
   double old_full_llik = -INFINITY;

   while (old_full_llik < new_full_llik) {

      // Update breakpoints.
      nbreaks++;
      for (i = 0 ; i < n ; i++) {
         for (j = 0 ; j < n ; j++) {
            old_bkpt_list[i+j*n] = new_bkpt_list[i+j*n];
         }
      }

      // Cycle over end point 'j'.
      for (j = 3 * nbreaks + 2 ; j < n ; j++) {
         new_llik[j] = -INFINITY;

         // Cycle over start point 'i'.
         for (i = 3 * nbreaks ; i < j - 1 ; i++) {

            // NAN if not a breakpoint, so next line evaluates to false.
            tmp = old_llik[i-1] + llik[i+j*n];
            if (tmp > new_llik[j]) {
               new_llik[j] = tmp;
               new_breakpoint = i-1;
            }
         }

         // Update breakpoint list.
         if (new_llik[j] > -INFINITY) {
            for (i = 0 ; i < n ; i++) {
               new_bkpt_list[j+i*n] = old_bkpt_list[new_breakpoint+i*n];
            }
            new_bkpt_list[j+new_breakpoint*n] = 1;
         }

      }

      // Update full log-likelihoods.
      old_full_llik = new_full_llik;
      new_full_llik = new_llik[n-1];
      for (i = 0 ; i < n ; i++) {
         old_llik[i] = new_llik[i];
      }

   }
   

   for (i = 0 ; i < n ; i++) {
      all_breakpoints[i] = old_bkpt_list[n-1+i*n];
   }

   free(new_bkpt_list);
   free(old_bkpt_list);

   return all_breakpoints;

}


int *tadbit(double **obs, int n, int m, int fast) {

   int i, j, k;

/*
   * Allocate memory and initialize variables. The distance
   * matrix 'dis' is the distance to the main diagonal. Every
   * element of coordinate (i,j) is on a diagonal; the distance
   * is the shift to the main diagonal |i-j|.
   * 'd_blk' and 'k_blk' will hold the distance data ('d_blk')
   * and observation data ('k_blk') when the matrices are
   * segmented. Each segmentation defines 3 regions, which is
   * why there are 3 such matrices. They are allocated the maximum
   * size they can have upon segmentation for simplicity.
   * 'ab' contains parameters 'a' and 'b' for the maximum likelihood
   * model. Because each segmentation defines 3 regions we need
   * 3 pairs of parameters.
*/

   double *dis = (double *) malloc(n*n * sizeof(double));
   double *llik = (double *) malloc(n*n * sizeof(double));

   k = 0;
   for (i = 0; i < n ; i++) {
      for (j = 0; j < n ; j++) {
         llik[k] = NAN;
         dis[k] = abs(i-j);
         k++;
      }
   }

   // Allocate max possible size to blocks.
   ml_blocks *blocks = (ml_blocks *) malloc(sizeof(ml_blocks));
   int nmax = (n+1)*(n+1)/4;

   blocks->k[0] = (double *) malloc(nmax     * sizeof(double));
   blocks->k[1] = (double *) malloc(nmax * 2 * sizeof(double));
   blocks->k[2] = (double *) malloc(nmax     * sizeof(double));
   blocks->d[0] = (double *) malloc(nmax     * sizeof(double));
   blocks->d[1] = (double *) malloc(nmax * 2 * sizeof(double));
   blocks->d[2] = (double *) malloc(nmax     * sizeof(double));


   // Initialize 'a' and 'b' to 0.
   double ab[3][2] = {{0.0,0.0}, {0.0,0.0}, {0.0,0.0}};

/*
   * If 'fast' is true, a heuristic is used to speed up the
   * algorithm. The log-likelihood is computed by inserting a
   * single break and local maxima are used as only candidate
   * breakpoints. Because tadbit is O(n^2) the gain is of the
   * same order.
*/
   
   int bkpts[n];
   // By default, all breakpoints are candidates.
   for (i = 0 ; i < n ; i++) {
      bkpts[i] = 1;
   }

   // If 'fast', only local maxima are candidates.
   if (fast) {
      remove_non_local_maxima(obs, dis, n, m, blocks, bkpts);
   }


/*
   * Compute the log-likelihood of the segments. the element
   * (i,j) of the matrix-like array 'llik' will contain the
   * log-likelihood of the segment starting at i and ending
   * at j. the matrix is initialized with nan because not all
   * elements will be computed. the lower triangular part is
   * left out and possibily most of the elements if fast is
   * true.
*/

   for (i = 0 ; i < n-2 ; i++) {
      // Skip if not a potential breakpoint.
      if ((i > 0) && (!bkpts[i-1])) {
         continue;
      }

      for (j = i+2 ; j < n ; j++) {
         // Skip if not a potential breakpoint.
         if (!bkpts[j]) {
            continue;
         }
         
         llik[i+j*n] = 0.0;
         for (k = 0 ; k < m ; k++) {
            slice(obs[k], dis, n, i, j, blocks);
            // Get the likelihood per block and sum.
            llik[i+j*n] +=
              ml_ab(blocks->k[0], blocks->d[0], ab[0], blocks->size[0]) / 2 +
              ml_ab(blocks->k[1], blocks->d[1], ab[1], blocks->size[1])     +
              ml_ab(blocks->k[2], blocks->d[2], ab[2], blocks->size[2]) / 2;
         }
      }
   }


/*
   * The matrix 'llik' contains the log-likelihood of the
   * segments. The breakpoints are found by the dynamic
   * programming routine 'get_breakpoints'.
*/

   int *all_breakpoints = (int *) malloc(n * sizeof(n));
   all_breakpoints = get_breakpoints(llik, n, all_breakpoints);

   // TODO: free blocks (?)
   free(dis);
   free(llik);

   // Done!!
   return all_breakpoints;

}
