/***********************************************************************
 *  
 * Copyright (C) 2008 Carsten Urbach
 *
 * Adapted from monomial.h by Florian Burger 2009/12/16
 *
 * More flexible handling of measurements parameters by Georg Bergner 2016
 *
 * This file is part of tmLQCD.
 *
 * tmLQCD is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * tmLQCD is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with tmLQCD.  If not, see <http://www.gnu.org/licenses/>.
 *
 *
 *
 ***********************************************************************/
#ifndef _MEASUREMENTS_H
#define _MEASUREMENTS_H

#define max_no_measurements 20

/* Give the measurement types an unambiguous ID*/
enum MEAS_TYPE { 
  ONLINE, 
  PIONNORM, 
  POLYAKOV, 
  ORIENTED_PLAQUETTES,
  GRADIENT_FLOW,
  REWEIGHTING
  };




typedef struct {
  enum MEAS_TYPE type;
  int initialised;
  int id;
  
  /* for polyakov loop */
  int direction;
  
  /* for maximal iterations in inversions for correlators */
  int max_iter;


  // random seed
  unsigned int seed;


  /* maximum number of slice, the source can be put
    if the correlator is measured in T(Z)-direction this will be set to 
    T(LZ) by init_measurements
  */
  int max_source_slice;


  void* parameter;

  /* frequency of the measurement */
  int freq;


  /* how it's usually called */
  char name[100];
  
  /* functions for the measurement */
  void (*measurefunc) (const int traj, const int id, const int ieo);
	void (*destructor)(void* param);
} measurement;


/* list of all monomials */
extern measurement measurement_list[max_no_measurements];
extern int no_measurements;

/* add a new measurement to the list of measurements */
int add_measurement(const enum MEAS_TYPE);
/* initialise all measurements in the list */
int init_measurements();
/* free space again */
void free_measurements();

void dummy_meas(const int traj, const int id, const int ieo);
void default_destructor(void* ptr);

#endif
