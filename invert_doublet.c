/* $Id$ */
/*******************************************************************************
*
* File hybrid.c
*
* Hybrid-Monte-Carlo for twisted mass QCD
*
* Author: Carsten Urbach
*         urbach@physik.fu-berlin.de
*
*******************************************************************************/

#define MAIN_PROGRAM

#include"lime.h"
#ifdef HAVE_CONFIG_H
# include<config.h>
#endif
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <string.h>
#include <signal.h>
#ifdef MPI
#include <mpi.h>
#endif
#include "global.h"
#include "getopt.h"
#include "linalg_eo.h"
#include "geometry_eo.h"
#include "start.h"
/*#include "eigenvalues.h"*/
#include "observables.h"
#ifdef MPI
#include "xchange.h"
#endif
#include "io.h"
#include "io_utils.h"
#include "propagator_io.h"
#include "gauge_io.h"
#include "read_input.h"
#include "mpi_init.h"
#include "sighandler.h"
#include "boundary.h"
#include "solver/solver.h"
#include "init_gauge_field.h"
#include "init_geometry_indices.h"
#include "init_spinor_field.h"
#include "init_moment_field.h"
#include "init_dirac_halfspinor.h"
#include "init_chi_spinor_field.h"
#include "xchange_halffield.h"
#include "update_backward_gauge.h"
#include "stout_smear.h"
#include "invert_eo.h"
#include "invert_doublet_eo.h"
#include "D_psi.h"
#include "phmc.h"
#include "Nondegenerate_Matrix.h"
#include "linalg/convert_eo_to_lexic.h"


void usage(){
  fprintf(stdout, "Inversion for EO preconditioned Wilson twisted mass QCD\n");
  fprintf(stdout, "This code inverts a (non-degenerate) doublet\n");
  fprintf(stdout, "Version %s \n\n", PACKAGE_VERSION);
  fprintf(stdout, "Please send bug reports to %s\n", PACKAGE_BUGREPORT);
  fprintf(stdout, "Usage:   invert [options]\n");
  fprintf(stdout, "Options: [-f input-filename]\n");
  fprintf(stdout, "         [-o output-filename]\n");
  fprintf(stdout, "         [-h|-? this help]\n");
  exit(0);
}

extern int nstore;

int check_geometry();

int main(int argc,char *argv[]) {

  FILE *parameterfile=NULL, *ifs=NULL;
  int c, iter, j, ix=0, is=0, ic=0, fl=0;
  char * filename = NULL;
  char datafilename[50];
  char parameterfilename[50];
  char conf_filename[50];
  char * input_filename = NULL;
  double plaquette_energy;

#ifdef _GAUGE_COPY
  int kb=0;
#endif
  double nrm1, nrm2;
#ifdef MPI
  double atime=0., etime=0.;
#endif
#ifdef _KOJAK_INST
#pragma pomp inst init
#pragma pomp inst begin(main)
#endif

  DUM_DERI = 8;
  DUM_SOLVER = DUM_DERI+5;
  DUM_MATRIX = DUM_SOLVER+6;
  /* DUM_MATRIX + 2 is enough (not 6) */
  NO_OF_SPINORFIELDS = DUM_MATRIX+8;

  verbose = 0;
  g_use_clover_flag = 0;
  g_nr_of_psf = 1;

#ifdef MPI
  MPI_Init(&argc, &argv);
#endif

  while ((c = getopt(argc, argv, "h?f:o:")) != -1) {
    switch (c) {
    case 'f': 
      input_filename = calloc(200, sizeof(char));
      strcpy(input_filename,optarg);
      break;
    case 'o':
      filename = calloc(200, sizeof(char));
      strcpy(filename,optarg);
      break;
    case 'h':
    case '?':
    default:
      usage();
      break;
    }
  }
  if(input_filename == NULL){
    input_filename = "hmc.input";
  }
  if(filename == NULL){
    filename = "output";
  } 

  /* Read the input file */
  read_input(input_filename);
  /* this DBW2 stuff is not needed for the inversion ! */
  g_rgi_C1 = 0;
  if(Nskip == 0){
    Nskip = 1;
  }
  mpi_init(argc, argv);

  g_dbw2rand = 0;

#ifndef MPI
  g_dbw2rand = 0;
#endif

#ifdef _GAUGE_COPY
  j = init_gauge_field(VOLUMEPLUSRAND, 1);
#else
  j = init_gauge_field(VOLUMEPLUSRAND, 0);
#endif
  if ( j!= 0) {
    fprintf(stderr, "Not enough memory for gauge_fields! Aborting...\n");
    exit(-1);
  }
  j = init_geometry_indices(VOLUMEPLUSRAND);
  if ( j!= 0) {
    fprintf(stderr, "Not enough memory for geometry indices! Aborting...\n");
    exit(-1);
  }
  if(even_odd_flag) {
    j = init_spinor_field(VOLUMEPLUSRAND/2, NO_OF_SPINORFIELDS);
  }
  else {
    j = init_spinor_field(VOLUMEPLUSRAND, NO_OF_SPINORFIELDS);
  }
  if ( j!= 0) {
    fprintf(stderr, "Not enough memory for spinor fields! Aborting...\n");
    exit(-1);
  }
  /* Chi`s-spinors  memory allocation */
  j = init_chi_up_spinor_field(VOLUMEPLUSRAND/2, 20);
  if ( j!= 0) {
    fprintf(stderr, "Not enough memory for PHMC Chi_up fields! Aborting...\n");
    exit(0);
  }
  j = init_chi_dn_spinor_field(VOLUMEPLUSRAND/2, 20);
  if ( j!= 0) {
    fprintf(stderr, "Not enough memory for PHMC Chi_dn fields! Aborting...\n");
    exit(0);
  }


  g_mu = g_mu1; 
  
  phmc_invmaxev = 1.;

  if(g_proc_id == 0){    
    /*construct the filenames for the observables and the parameters*/
    strcpy(datafilename,filename);  strcat(datafilename,".data");
    strcpy(parameterfilename,filename);  strcat(parameterfilename,".para");
    
    parameterfile=fopen(parameterfilename, "w");
    write_first_messages(parameterfile, 0, 1);
  }

  /* define the geometry */
  geometry();

  /* define the boundary conditions for the fermion fields */
  boundary();

#ifdef _USE_HALFSPINOR
  j = init_dirac_halfspinor();
  if ( j!= 0) {
    fprintf(stderr, "Not enough memory for halffield! Aborting...\n");
    exit(-1);
  }
  if(g_sloppy_precision_flag == 1) {
    j = init_dirac_halfspinor32();
    if ( j!= 0) {
      fprintf(stderr, "Not enough memory for 32-Bit halffield! Aborting...\n");
      exit(-1);
    }
  }
#  if (defined _PERSISTENT)
  init_xchange_halffield();
#  endif
#endif

  for(j=0;j<Nmeas; j++) {
    sprintf(conf_filename,"%s.%.4d", gauge_input_filename, nstore);
    if (g_proc_id == 0){
      printf("Reading Gauge field from file %s\n", conf_filename); fflush(stdout);
    }
    if(gauge_precision_read_flag == 64) {
      read_lime_gauge_field(conf_filename);
    }
    else if(gauge_precision_read_flag == 32){
      read_lime_gauge_field_singleprec(conf_filename);
    }
    if (g_proc_id == 0){
      printf("done!\n"); fflush(stdout);
    }
#ifdef MPI
    xchange_gauge();
#endif
#ifdef _GAUGE_COPY
    update_backward_gauge();
#endif

    /*compute the energy of the gauge field*/
    plaquette_energy = measure_gauge_action();

    if(g_proc_id == 0) {
      printf("The plaquette value is %e\n", plaquette_energy/(6.*VOLUME*g_nproc)); fflush(stdout);
    }

    if(use_stout_flag == 1) {
      if( stout_smear_gauge_field(stout_rho , stout_no_iter) != 0 ) exit(1) ;

      plaquette_energy = measure_gauge_action();

      if(g_proc_id == 0) {
	printf("The plaquette value after stouting is %e\n", plaquette_energy/(6.*VOLUME*g_nproc)); fflush(stdout);
      }
    }

    for(ix = index_start; ix < index_end; ix++) {
      is = (ix / 3);
      ic = (ix % 3);
      for(fl = 0; fl < 2; fl++) {
	zero_spinor_field(g_spinor_field[0], VOLUME/2);
	zero_spinor_field(g_spinor_field[1], VOLUME/2);
	zero_spinor_field(g_spinor_field[2], VOLUME/2);
	zero_spinor_field(g_spinor_field[3], VOLUME/2);
	if(read_source_flag == 0) {
	  if(source_location == 0) {
	    source_spinor_field(g_spinor_field[0+fl*2], g_spinor_field[1+fl*2], is, ic);
	  }
	  else {
	    source_spinor_field_point_from_file(g_spinor_field[0+fl*2], g_spinor_field[1+fl*2], 
						is, ic, source_location);
	  }
	}
	else {
	  if(source_splitted) {
	    sprintf(conf_filename,"%s.%.2d", source_input_filename, ix);
	    if(g_proc_id == 0) {
	      printf("Reading source from %s\n", conf_filename);
	    }
	    if(read_lime_spinor(g_spinor_field[0+fl*2], g_spinor_field[1+fl*2], conf_filename, 0) != 0) {
	      if(g_proc_id == 0) {
		printf("Error reading source! Aborting...\n");
	      }
#ifdef MPI
	      MPI_Abort(MPI_COMM_WORLD, 1);
	      MPI_Finalize();
#endif
	      exit(-1);
	    };
	  }
	  else {
	    sprintf(conf_filename,"%s", source_input_filename);
	    if(g_proc_id == 0) {
	      printf("Reading source from %s\n", conf_filename);
	    }
	    if(read_lime_spinor(g_spinor_field[0+2*fl], g_spinor_field[1+2*fl], conf_filename, ix) != 0) {
	      if(g_proc_id == 0) {
		printf("Error reading source! Aborting...\n");
	      }
#ifdef MPI
	      MPI_Abort(MPI_COMM_WORLD, 1);
	      MPI_Finalize();
#endif
	      exit(-1);
	    };
	  }
	}

	mul_one_pm_itau2(g_spinor_field[4], g_spinor_field[6], g_spinor_field[0], g_spinor_field[2], +1., VOLUME/2);
	mul_one_pm_itau2(g_spinor_field[5], g_spinor_field[7], g_spinor_field[1], g_spinor_field[3], +1., VOLUME/2);
	assign(g_spinor_field[0], g_spinor_field[4], VOLUME/2);
	assign(g_spinor_field[1], g_spinor_field[5], VOLUME/2);
	assign(g_spinor_field[2], g_spinor_field[6], VOLUME/2);
	assign(g_spinor_field[3], g_spinor_field[7], VOLUME/2);

	if(g_proc_id == 0) {printf("mubar = %e, epsbar = %e\n", g_mubar, g_epsbar);}
	
	if(propagator_splitted) {
	  sprintf(conf_filename,"%s.%.2d.hinverted", source_input_filename, ix);
	}
	else {
	  sprintf(conf_filename,"%s.hinverted", source_input_filename);
	}
	
	
#ifdef MPI
	atime = MPI_Wtime();
#endif
	iter = invert_doublet_eo(g_spinor_field[4], g_spinor_field[5], g_spinor_field[6], g_spinor_field[7], 
				 g_spinor_field[0], g_spinor_field[1], g_spinor_field[2], g_spinor_field[3], 
				 solver_precision, max_solver_iterations, solver_flag,g_relative_precision_flag);
#ifdef MPI
	etime = MPI_Wtime();
#endif
	
	/* To write in standard format */
	/* we have to mult. by 2*kappa */
	mul_r(g_spinor_field[DUM_DERI], (2*g_kappa), g_spinor_field[4], VOLUME/2);
	mul_r(g_spinor_field[DUM_DERI+1], (2*g_kappa), g_spinor_field[5], VOLUME/2);
	mul_r(g_spinor_field[DUM_DERI+2], (2*g_kappa), g_spinor_field[6], VOLUME/2);
	mul_r(g_spinor_field[DUM_DERI+3], (2*g_kappa), g_spinor_field[7], VOLUME/2);

	mul_one_pm_itau2(g_spinor_field[4], g_spinor_field[6], g_spinor_field[DUM_DERI], g_spinor_field[DUM_DERI+2],
			 -1., VOLUME/2);
	mul_one_pm_itau2(g_spinor_field[5], g_spinor_field[7], g_spinor_field[DUM_DERI+1], g_spinor_field[DUM_DERI+3], 
			 -1., VOLUME/2);

	
	if(propagator_splitted) {
	  if(fl == 0) {
	    write_propagator_type(write_prop_format_flag, conf_filename);
	  }
	  write_xlf_info(plaquette_energy/(6.*VOLUME*g_nproc), nstore, conf_filename, 1);
	  /* write the source depending on format */
	  if(write_prop_format_flag == 1 && fl == 0) {
	    write_source(g_spinor_field[0], g_spinor_field[1], conf_filename, 1, 32);

	  }
	  write_double_propagator(g_spinor_field[4], g_spinor_field[5],
				  g_spinor_field[6], g_spinor_field[7], conf_filename, 1, prop_precision_flag);
	}
	else {
	  /* 	sprintf(conf_filename,"%s%.2d.%.4d", "prop.mass", mass_number, nstore); */
	  if(ix == index_start && fl == 0) {
	    write_propagator_type(write_prop_format_flag, conf_filename);
	  }
	  write_xlf_info(plaquette_energy/(6.*VOLUME*g_nproc), nstore, conf_filename, 1);
	  /* write the source depending on format */
	  if(write_prop_format_flag == 1 && fl == 0) {
	    write_source(g_spinor_field[0], g_spinor_field[1], conf_filename, 1, 32);
	  }
	  write_double_propagator(g_spinor_field[4], g_spinor_field[5],
				  g_spinor_field[6], g_spinor_field[7], conf_filename, 1, prop_precision_flag);
	}


	if(fabs(g_epsbar) < 0.0000001) {
	  /* Check the result */
	  M_full(g_spinor_field[6], g_spinor_field[7], g_spinor_field[4], g_spinor_field[5]); 
	  mul_r(g_spinor_field[6], 1./(2*g_kappa), g_spinor_field[6], VOLUME/2);  
	  mul_r(g_spinor_field[7], 1./(2*g_kappa), g_spinor_field[7], VOLUME/2); 
	  
	  diff(g_spinor_field[6], g_spinor_field[6], g_spinor_field[0], VOLUME/2); 
	  diff(g_spinor_field[7], g_spinor_field[7], g_spinor_field[1], VOLUME/2); 
	  
	  nrm1 = square_norm(g_spinor_field[6], VOLUME/2); 
	  nrm2 = square_norm(g_spinor_field[7], VOLUME/2); 
	  
	  if(g_proc_id == 0) {
	    printf("Inversion for source %d done in %d iterations, residue = %e!\n", 2*ix+fl, iter, nrm1+nrm2);
	  }
	}
#ifdef MPI
	if(g_proc_id == 0) {
	  printf("Inversion done in %e sec. (MPI_Wtime)\n", etime-atime);
	}
#endif
      }
      if(g_proc_id == 0) {
	write_inverter_info(solver_precision, iter, 0, 1, conf_filename);
      }
    }
    nstore+=Nskip;
  }
  
#ifdef MPI
  MPI_Finalize();
#endif
  free_gauge_field();
  free_geometry_indices();
  free_spinor_field();
  free_moment_field();
  return(0);
#ifdef _KOJAK_INST
#pragma pomp inst end(main)
#endif
}