/* $Id$ */

/*******************************************************************************
 * Generalized minimal residual (GMRES) with a maximal number of restarts.    
 * Solves Q=AP for complex regular matrices A.
 * For details see: Andreas Meister, Numerik linearer Gleichungssysteme        
 *   or the original citation:                                                 
 * Y. Saad, M.H.Schultz in GMRES: A generalized minimal residual algorithm    
 *                         for solving nonsymmetric linear systems.            
 * 			SIAM J. Sci. Stat. Comput., 7: 856-869, 1986           
 *           
 * int gmres(spinor * const P,spinor * const Q, 
 *	   const int m, const int max_restarts,
 *	   const double eps_sq, matrix_mult f)
 *                                                                 
 * Returns the number of iterations needed or -1 if maximal number of restarts  
 * has been reached.                                                           
 *
 * Inout:                                                                      
 *  spinor * P       : guess for the solving spinor                                             
 * Input:                                                                      
 *  spinor * Q       : source spinor
 *  int m            : Maximal dimension of Krylov subspace                                     
 *  int max_restarts : maximal number of restarts                                   
 *  double eps       : stopping criterium                                                     
 *  matrix_mult f    : pointer to a function containing the matrix mult
 *                     for type matrix_mult see matrix_mult_typedef.h
 *
 * Autor: Carsten Urbach <urbach@ifh.de>
 ********************************************************************************/

#ifdef HAVE_CONFIG_H
# include<config.h>
#endif
#include<stdlib.h>
#include<stdio.h>
#include<math.h>
#include"global.h"
#include"su3.h"
#include"linalg_eo.h"
#include"diagonalise_general_matrix.h"
#include"quicksort.h"
#include"linalg/lapack.h"
#include"linalg/blas.h"
#include"gmres_dr.h"

static void init_gmres(const int _M, const int _V);
complex short_scalar_prod(complex * const x, complex * const y, const int N);
void short_ModifiedGS(complex v[], int n, int m, complex A[]);

static complex ** work;
static complex * _work;
static complex ** work2;
static complex * _work2;
static complex ** H;
static complex ** G;
static complex * alpha;
static complex * c;
static double * s;
static spinor ** V;
static spinor * _v;
static spinor ** Z;
static spinor * _z;
static complex * _h;
static complex * _g;
static complex * alpha;
static complex * c;
static double * s;
static complex * evalues;
static double * sortarray;
static int * idx;
static int one = 1;
static double mone = -1.;
static double pone = 1.;
static complex cmone;
static complex cpone;
static complex czero;

int gmres_dr(spinor * const P,spinor * const Q, 
	  const int m, const int nr_ev, const int max_restarts,
	  const double eps_sq, const int rel_prec,
	  const int N, matrix_mult f){

  int restart=0, i, j, k, l;
  double beta, eps, norm, beta2;
  complex *lswork = NULL;
  int lwork;
  complex tmp1, tmp2;
  int info=0;
  int _m = m, mp1 = m+1, np1 = nr_ev+1, ne = nr_ev, V2 = 12*(VOLUME + RAND)/2, _N = 12*N;
/*   init_solver_field(3); */
  cmone.re = -1.; cmone.im=0.;
  cpone.re = 1.; cpone.im=0.;
  czero.re = 0.; czero.im = 0.;
  eps=sqrt(eps_sq);
  
  init_gmres(m, (VOLUME + RAND)/2);
  norm = sqrt(square_norm(Q, N));

  assign(g_spinor_field[DUM_SOLVER+2], P, N);

  /* first normal GMRES cycle */
/*    for(restart = 0; restart < max_restarts; restart++) {  */
  /* r_0=Q-AP  (b=Q, x+0=P) */
  f(g_spinor_field[DUM_SOLVER], g_spinor_field[DUM_SOLVER+2]);
  diff(g_spinor_field[DUM_SOLVER], Q, g_spinor_field[DUM_SOLVER], N);
  
  /* v_0=r_0/||r_0|| */
  alpha[0].re=sqrt(square_norm(g_spinor_field[DUM_SOLVER], N));
  
  if(g_proc_id == g_stdio_proc && g_debug_level > 0){
    printf("%d\t%g true residue\n", restart*m, alpha[0].re*alpha[0].re); 
    fflush(stdout);
  }
  
  if(alpha[0].re==0.){
    assign(P, g_spinor_field[DUM_SOLVER+2], N);
    return(restart*m);
  }
  
  mul_r(V[0], 1./alpha[0].re, g_spinor_field[DUM_SOLVER], N);
  
  for(j = 0; j < m; j++){
    /* g_spinor_field[DUM_SOLVER]=A*v_j */
    
    f(g_spinor_field[DUM_SOLVER], V[j]);
    
    /* Set h_ij and omega_j */
    /* g_spinor_field[DUM_SOLVER+1] <- omega_j */
    assign(g_spinor_field[DUM_SOLVER+1], g_spinor_field[DUM_SOLVER], N);
    for(i = 0; i <= j; i++){
      H[i][j] = scalar_prod(V[i], g_spinor_field[DUM_SOLVER+1], N);
      /* G and work are in Fortran storage: columns first */
      G[j][i] = H[i][j];
      work2[j][i] = H[i][j];
      work[i][j].re = H[i][j].re;
      work[i][j].im = -H[i][j].im;
      assign_diff_mul(g_spinor_field[DUM_SOLVER+1], V[i], H[i][j], N);
    }
    
    _complex_set(H[j+1][j], sqrt(square_norm(g_spinor_field[DUM_SOLVER+1], N)), 0.);
    G[j][j+1] = H[j+1][j];
    work2[j][j+1] = H[j+1][j];
    work[j+1][j].re =  H[j+1][j].re;
    work[j+1][j].im =  -H[j+1][j].im;
    beta2 = H[j+1][j].re*H[j+1][j].re;
    for(i = 0; i < j; i++){
      tmp1 = H[i][j];
      tmp2 = H[i+1][j];
      _mult_real(H[i][j], tmp2, s[i]);
      _add_assign_complex_conj(H[i][j], c[i], tmp1);
      _mult_real(H[i+1][j], tmp1, s[i]);
      _diff_assign_complex(H[i+1][j], c[i], tmp2);
    }
    
    /* Set beta, s, c, alpha[j],[j+1] */
    beta = sqrt(_complex_square_norm(H[j][j]) + _complex_square_norm(H[j+1][j]));
    s[j] = H[j+1][j].re / beta;
    _mult_real(c[j], H[j][j], 1./beta);
    _complex_set(H[j][j], beta, 0.);
    _mult_real(alpha[j+1], alpha[j], s[j]);
    tmp1 = alpha[j];
    _mult_assign_complex_conj(alpha[j], c[j], tmp1);
    
    /* precision reached? */
    if(g_proc_id == g_stdio_proc && g_debug_level > 0){
      printf("%d\t%g residue\n", restart*m+j, alpha[j+1].re*alpha[j+1].re); 
      fflush(stdout);
    }
    if(((alpha[j+1].re <= eps) && (rel_prec == 0)) || ((alpha[j+1].re <= eps*norm) && (rel_prec == 1))){
      _mult_real(alpha[j], alpha[j], 1./H[j][j].re);
      assign_add_mul(g_spinor_field[DUM_SOLVER+2], V[j], alpha[j], N);
      for(i = j-1; i >= 0; i--){
	for(k = i+1; k <= j; k++){
	  _mult_assign_complex(tmp1, H[i][k], alpha[k]); 
	  /* alpha[i] -= tmp1 */
	  _diff_complex(alpha[i], tmp1);
	}
	_mult_real(alpha[i], alpha[i], 1./H[i][i].re);
	assign_add_mul(g_spinor_field[DUM_SOLVER+2], V[i], alpha[i], N);
      }
      for(i = 0; i < m; i++){
	alpha[i].im = 0.;
      }
      assign(P, g_spinor_field[DUM_SOLVER+2], N);
      return(restart*m+j);
    }
    /* if not */
    else{
      mul_r(V[(j+1)], 1./H[j+1][j].re, g_spinor_field[DUM_SOLVER+1], N);
    }
    
  }
  j=m-1;
  /* prepare for restart */
  _mult_real(alpha[j], alpha[j], 1./H[j][j].re);
  assign_add_mul(g_spinor_field[DUM_SOLVER+2], V[j], alpha[j], N);
  for(i = j-1; i >= 0; i--){
    for(k = i+1; k <= j; k++){
      _mult_assign_complex(tmp1, H[i][k], alpha[k]);
      _diff_complex(alpha[i], tmp1);
    }
    _mult_real(alpha[i], alpha[i], 1./H[i][i].re);
    assign_add_mul(g_spinor_field[DUM_SOLVER+2], V[i], alpha[i], N);
  }
  /* r_0=Q-AP  (b=Q, x+0=P) */
  f(g_spinor_field[DUM_SOLVER], g_spinor_field[DUM_SOLVER+2]);
  diff(g_spinor_field[DUM_SOLVER], Q, g_spinor_field[DUM_SOLVER], N);

  /* v_0=r_0/||r_0|| */
  tmp1.re=sqrt(square_norm(g_spinor_field[DUM_SOLVER], N));
  
  if(g_proc_id == g_stdio_proc && g_debug_level > 0){
    printf("%d\t%g true residue\n", restart*m, alpha[0].re*alpha[0].re); 
    fflush(stdout);
  }
  
  if(tmp1.re==0.){
    assign(P, g_spinor_field[DUM_SOLVER+2], N);
    return(restart*m);
  }
  
  for(restart = 1; restart < max_restarts; restart++) {
    /* compute Eigenvalues and vectors */
    for(i = 0; i < m; i++){
      c[i].re = 0.;
      c[i].im = 0.;
    }
    c[m-1].re = 1.;
    _FT(zgesv) (&_m, &one, work[0], &mp1, idx, c, &_m, &info); 
    G[m-1][m-1].re += (beta2*c[idx[m-1]-1].re);
    G[m-1][m-1].im += (beta2*c[idx[m-1]-1].im);
    if(g_proc_id == 0 && g_debug_level > 1){
      printf("zgesv returned info = %d, c[m-1]= %e, %e , idx[m-1]=%d\n", info, c[idx[m-1]-1].re, c[idx[m-1]-1].im, idx[m-1]);
    }
    /* compute c-\bar H \alpha */
    work[nr_ev][0].re = sqrt(beta2);
    work[nr_ev][0].im = 0.;
    for(i=1; i < m+1; i++) {
      work[nr_ev][i].re = 0.;
      work[nr_ev][i].im = 0.;
    }
    _FT(zgemv) ("N", &mp1, &_m, &cmone, G[0], &mp1, alpha, &one, &cpone, work[nr_ev], &one, 1);
    diagonalise_general_matrix(m, G[0], mp1, c, evalues);
    for(i = 0; i < m; i++) {
      sortarray[i] = _complex_square_norm(evalues[i]);
      idx[i] = i;
    }
    quicksort(m, sortarray, idx);
    if(g_proc_id == g_stdio_proc && g_debug_level > 1) {
      for(i = 0; i < m; i++) {
	printf("Evalues %d %e  %e \n", i, evalues[idx[i]].re, evalues[idx[i]].im);
      }
      fflush(stdout);
    }
    
    /* Copy the first nr_ev eigenvectors to work */
    for(i = 0; i < nr_ev; i++) {
      for(l = 0; l < m; l++) {
	work[i][l] = G[idx[i]][l];
      }
    }
    /* Orthonormalize them */
    for(i = 0; i < nr_ev; i++) {
      work[i][m].re = 0.;
      work[i][m].im = 0.;
      short_ModifiedGS(work[i], m+1, i, work[0]); 
    }
    /* Orthonormalize c - \bar H d to work */
    short_ModifiedGS(work[nr_ev], m+1, nr_ev, work[0]);
    
    /* Now compute \bar H = P^T_k+1 \bar H_m P_k */
    _FT(zgemm) ("N", "N", &mp1, &ne, &_m, &cpone, work2[0], &mp1, work[0], &mp1, &czero, G[0], &mp1, 1, 1); 
    _FT(zgemm) ("C", "N", &np1, &ne , &mp1, &cpone, work[0], &mp1, G[0], &mp1, &czero, H[0], &mp1, 1, 1); 
    for(i = 0; i < nr_ev+1; i++) {
      for(l = 0; l < nr_ev+1; l++) {
	if(g_proc_id == 0 && g_debug_level > 3) {
	  printf("(g[%d], g[%d]) = %e, %e\n", i, l, short_scalar_prod(work[i], work[l], m+1).re, 
		 short_scalar_prod(work[i], work[l], m+1).im);
	}
      }
    }
    /* V_k+1 = V_m+1 P_k+1 */
    _FT(zgemm) ("N", "N", &_N, &np1, &mp1, &cpone, (complex*)V[0], &V2, work[0], &mp1, &czero, (complex*)Z[0], &V2, 1, 1); 
    /* copy back to V */
    _FT(zlacpy) ("A", &_N, &np1, (complex*)Z[0], &V2, (complex*)V[0], &V2, 1); 
    if(g_debug_level > 3) {
      for(i = 0; i < np1; i++) {
	for(l = 0; l < np1; l++) {
	  alpha[0] = scalar_prod(V[l], V[i], N);
	  if(g_proc_id == 0) {
	    printf("(V[%d], V[%d]) = %e %e %d %d %d %d %d %d\n", l, i, alpha[0].re, alpha[0].im, np1, mp1, ne, _m, _N, V2);
	  }
	}
      }
    }
    /* } */
    
    
    for(i = 0; i < m+1; i++){
      alpha[i].im = 0.;
      for(l = 0; l < m+1; l++) {
	G[i][l].re = 0.; G[i][l].im=0.;
	work[i][l].re = 0.; work[i][l].im=0.;
	work2[i][l].re = 0.; work2[i][l].im=0.;
      }
    }
    for(i=0; i < ne; i++) { 
      for(l = 0; l < np1; l++) { 
 	G[i][l] = H[i][l];
	work2[i][l] = H[i][l];
	work[l][i].re = H[i][l].re;
	work[l][i].im = -H[i][l].im;
      } 
    }
        
    for(j = nr_ev; j < m; j++){
      /* g_spinor_field[DUM_SOLVER]=A*v_j */
      
      f(g_spinor_field[DUM_SOLVER], V[j]);
      
      /* Set h_ij and omega_j */
      /* g_spinor_field[DUM_SOLVER+1] <- omega_j */
      assign(g_spinor_field[DUM_SOLVER+1], g_spinor_field[DUM_SOLVER], N);
      for(i = 0; i <= j; i++){
	H[j][i] = scalar_prod(V[i], g_spinor_field[DUM_SOLVER+1], N);
	/* H, G and work are now all in Fortran storage: columns first */
	G[j][i] = H[j][i];
	work2[j][i] = H[j][i];
	work[i][j].re = H[j][i].re;
	work[i][j].im = -H[j][i].im;
	assign_diff_mul(g_spinor_field[DUM_SOLVER+1], V[i], H[j][i], N);
      }
      
      _complex_set(H[j][j+1], sqrt(square_norm(g_spinor_field[DUM_SOLVER+1], N)), 0.);
      G[j][j+1] = H[j][j+1];
      work2[j][j+1] = H[j][j+1];
      work[j+1][j].re =  H[j][j+1].re;
      work[j+1][j].im =  -H[j][j+1].im;
      beta2 = H[j][j+1].re*H[j][j+1].re;
      
      mul_r(V[(j+1)], 1./H[j][j+1].re, g_spinor_field[DUM_SOLVER+1], N);
      
    }
    for(i = 0; i < mp1; i++) {
      alpha[i] = scalar_prod(V[i], g_spinor_field[DUM_SOLVER], N);
    }
    /* Solve the least square problem */
    if(lswork == NULL) {
      lwork = -1;
      _FT(zgels) ("N", &mp1, &_m, &one, H[0], &mp1, alpha, &mp1, &tmp1, &lwork, &info, 1);
      lwork = (int)tmp1.re;
      lswork = (complex*)malloc(lwork*sizeof(complex));
    }
    _FT(zgels) ("N", &mp1, &_m, &one, H[0], &mp1, alpha, &mp1, lswork, &lwork, &info, 1);
    if(g_proc_id == 0 && g_debug_level > 0) {
      printf("zgels returned info = %d\n", info);
      fflush(stdout);
    }
    /* Compute the new solution vector */
    for(i = m-1; i >= 0; i--){
      assign_add_mul(g_spinor_field[DUM_SOLVER+2], V[i], alpha[i], N);
    }
    /* r_0=Q-AP  (b=Q, x+0=P) */
    f(g_spinor_field[DUM_SOLVER], g_spinor_field[DUM_SOLVER+2]);
    diff(g_spinor_field[DUM_SOLVER], Q, g_spinor_field[DUM_SOLVER], N);
    /* v_0=r_0/||r_0|| */
    tmp1.re=sqrt(square_norm(g_spinor_field[DUM_SOLVER], N));
    if(g_proc_id == 0) {
      printf("%d\t%e residue\n", restart*m, tmp1.re);
    }
  }


  /* If maximal number of restart is reached */
  assign(P, g_spinor_field[DUM_SOLVER+2], N);

  return(-1);
}

complex short_scalar_prod(complex * const y, complex * const x, const int N) {
  complex res;
  int ix;

  res.re = 0.;
  res.im = 0.;
  for (ix = 0; ix < N; ix++){
    res.re += +x[ix].re*y[ix].re + x[ix].im*y[ix].im;
    res.im += -x[ix].re*y[ix].im + x[ix].im*y[ix].re;
  }
  return(res);

}

void short_ModifiedGS(complex v[], int n, int m, complex A[]) {

  int i;
  complex s;

  for (i = 0; i < m; i++) {
    s = short_scalar_prod(A+i*n, v, n);
    s.re = -s.re; s.im = -s.im;
    _FT(zaxpy)(&n, &s, A+i*n, &one, v, &one); 
  }
  s.re = sqrt(short_scalar_prod(v, v, n).re);
  for(i = 0; i < n; i++) {
    v[i].re /= s.re;
    v[i].im /= s.re;
  }
}

static void init_gmres(const int _M, const int _V){
  static int Vo = -1;
  static int M = -1;
  static int init = 0;
  int i;

  if((M != _M)||(init == 0)||(Vo != _V)){
    if(init == 1){
      free(Z);
      free(_z);
      free(H);
      free(G);
      free(V);
      free(_h);
      free(_g);
      free(_v);
      free(alpha);
      free(c);
      free(s);
      free(evalues);
      free(work);
      free(_work);
      free(work2);
      free(_work2);
    }
    Vo = _V;
    M = _M;
    H = calloc(M+1, sizeof(complex *));
    Z = calloc(M+1, sizeof(complex *));
    G = calloc(M+1, sizeof(complex *));
    V = calloc(M+1, sizeof(spinor *));
    work = calloc(M+1, sizeof(complex *));
    work2 = calloc(M+1, sizeof(complex *));
#if (defined SSE || defined SSE2)
    _h = calloc((M+2)*(M+1), sizeof(complex));
    H[0] = (complex *)(((unsigned int)(_h)+ALIGN_BASE)&~ALIGN_BASE); 
    _work = calloc((M+2)*(M+1), sizeof(complex));
    work[0] = (complex *)(((unsigned int)(_work)+ALIGN_BASE)&~ALIGN_BASE); 
    _work2 = calloc((M+2)*(M+1), sizeof(complex));
    work2[0] = (complex *)(((unsigned int)(_work2)+ALIGN_BASE)&~ALIGN_BASE); 
    _g = calloc((M+2)*(M+1), sizeof(complex));
    G[0] = (complex *)(((unsigned int)(_g)+ALIGN_BASE)&~ALIGN_BASE); 
    _v = calloc((M+1)*Vo+1, sizeof(spinor));
    V[0] = (spinor *)(((unsigned int)(_v)+ALIGN_BASE)&~ALIGN_BASE);
    _z = calloc((M)*Vo+1, sizeof(spinor));
    Z[0] = (spinor *)(((unsigned int)(_z)+ALIGN_BASE)&~ALIGN_BASE);
#else
    _h = calloc((M+1)*(M+1), sizeof(complex));
    H[0] = _h;
    _work = calloc((M+1)*(M+1), sizeof(complex));
    work[0] = _work;
    _work2 = calloc((M+1)*(M+1), sizeof(complex));
    work2[0] = _work2;
    _g = calloc((M+1)*(M+1), sizeof(complex));
    G[0] = _g;
    _v = calloc(M*Vo, sizeof(spinor));
    V[0] = _v;
    _z = calloc(M*Vo, sizeof(spinor));
    Z[0] = _z;
#endif
    s = calloc(M, sizeof(double));
    c = calloc(M+1, sizeof(complex));
    alpha = calloc(M+1, sizeof(complex));
    evalues = calloc(M+1, sizeof(complex));
    sortarray = calloc(M+1, sizeof(double));
    idx = calloc(M+1, sizeof(int));
    for(i = 1; i < M; i++){
      V[i] = V[i-1] + Vo;
      H[i] = H[i-1] + M+1;
      Z[i] = Z[i-1] + Vo;
      G[i] = G[i-1] + M+1;
      work[i] = work[i-1] + M+1;
      work2[i] = work2[i-1] + M+1;
    }
    work[M] = work[M-1] + M+1;
    work2[M] = work2[M-1] + M+1;
    H[M] = H[M-1] + M+1;
    G[M] = G[M-1] + M+1;
    V[M] = V[M-1] + Vo;
    init = 1;
  }
}