#pragma once

#include <su3.h>
#include <buffers/gauge.h>
#include <buffers/adjoint.h>

/* We need a number of indices to do the bookkeeping.
   This will always amount to at most 12 fields, but
   we define some aliases so that we don't have to do
   the mental mapping all the time. */

void generic_staples(su3 *out, unsigned int x, unsigned int mu, gauge_field_t in);
void project_antiherm(su3 *in);
void project_herm(su3 *in);
void reunitarize(su3 *in);

inline void cayley_hamilton_exponent(su3 *expiQ, su3 const *Q);
void cayley_hamilton_exponent_with_force_terms(su3 *expiQ, su3 *B1, su3 *B2, double *f1, double *f2, su3 const *Q);

void print_su3(su3 *in);
void print_config_to_screen(gauge_field_t in);

#include "utils.inline"
