/* Copyright (C) 2003 Massachusetts Institute of Technology
%
%  This program is free software; you can redistribute it and/or modify
%  it under the terms of the GNU General Public License as published by
%  the Free Software Foundation; either version 2, or (at your option)
%  any later version.
%
%  This program is distributed in the hope that it will be useful,
%  but WITHOUT ANY WARRANTY; without even the implied warranty of
%  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
%  GNU General Public License for more details.
%
%  You should have received a copy of the GNU General Public License
%  along with this program; if not, write to the Free Software Foundation,
%  Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "meep.h"
#include "meep_internals.h"
#include "ran.h"

#define RESTRICT

inline double it(int cmp, double *(f[2]), int ind) { return (1-2*cmp)*f[1-cmp][ind]; }

inline int rstart_0(const volume &v, int m) {
  return (int) max(0.0, m - (int)(v.origin.r()*v.a+0.5) - 1.0);
}
inline int rstart_1(const volume &v, int m) {
  return (int) max(1.0, (double)m - (int)(v.origin.r()*v.a+0.5));
}

void fields::step_h() {
  for (int i=0;i<num_chunks;i++)
    if (chunks[i]->is_mine())
      chunks[i]->step_h();
}

void fields_chunk::step_h() {
  const volume v = this->v;
  if (v.dim != Dcyl) {
    const int n1 = num_each_direction[0];
    const int n2 = num_each_direction[1];
    const int n3 = num_each_direction[2];
    const int s1 = stride_each_direction[0];
    const int s2 = stride_each_direction[1];
    const int s3 = stride_each_direction[2];
    DOCMP FOR_MAGNETIC_COMPONENTS(cc)
      if (f[cc][cmp]) {
        const int yee_idx = v.yee_index(cc);
        const component c_p=plus_component[cc], c_m=minus_component[cc];
        const direction d_deriv_p = plus_deriv_direction[cc];
        const direction d_deriv_m = minus_deriv_direction[cc];
        const bool have_p = have_plus_deriv[cc];
        const bool have_m = have_minus_deriv[cc];
        const bool have_p_pml = have_p && ma->C[d_deriv_p][cc];
        const bool have_m_pml = have_m && ma->C[d_deriv_m][cc];
        const int stride_p = (have_p)?v.stride(d_deriv_p):0;
        const int stride_m = (have_m)?v.stride(d_deriv_m):0;
        // The following lines "promise" the compiler that the values of
        // these arrays won't change during this loop.
        RESTRICT const double *C_m = (have_m_pml)?ma->C[d_deriv_m][cc] + yee_idx:NULL;
        RESTRICT const double *C_p = (have_p_pml)?ma->C[d_deriv_p][cc] + yee_idx:NULL;
        RESTRICT const double *decay_m = (!have_m_pml)?NULL:
          ma->Cdecay[d_deriv_m][cc][component_direction(cc)] + yee_idx;
        RESTRICT const double *decay_p = (!have_p_pml)?NULL:
          ma->Cdecay[d_deriv_p][cc][component_direction(cc)] + yee_idx;
        RESTRICT const double *f_p = (have_p)?f[c_p][cmp] + v.yee_index(c_p):NULL;
        RESTRICT const double *f_m = (have_m)?f[c_m][cmp] + v.yee_index(c_m):NULL;
        RESTRICT double *the_f = f[cc][cmp] + yee_idx;
        RESTRICT double *the_f_p_pml = f_p_pml[cc][cmp] + yee_idx;
        RESTRICT double *the_f_m_pml = f_m_pml[cc][cmp] + yee_idx;
#include "step_h.h"
      }
  } else if (v.dim == Dcyl) {
    DOCMP {
      // Propogate Hr
      if (ma->C[Z][Hr])
        for (int r=rstart_1(v,m);r<=v.nr();r++) {
            double oor = 1.0/((int)(v.origin.r()*v.a+0.5) + r);
            double mor = m*oor;
            const int ir = r*(v.nz()+1);
            for (int z=0;z<v.nz();z++) {
              const double Czhr = ma->C[Z][Hr][z+ir];
              const double ooop_Czhr = ma->Cdecay[Z][Hr][R][z+ir];
              double dhrp = c*(-it(cmp,f[Ez],z+ir)*mor);
              double hrz = f[Hr][cmp][z+ir] - f_p_pml[Hr][cmp][z+ir];
              f_p_pml[Hr][cmp][z+ir] += dhrp;
              f[Hr][cmp][z+ir] += dhrp +
                ooop_Czhr*(c*(f[Ep][cmp][z+ir+1]-f[Ep][cmp][z+ir]) - Czhr*hrz);
            }
          }
      else
        for (int r=rstart_1(v,m);r<=v.nr();r++) {
            double oor = 1.0/((int)(v.origin.r()*v.a + 0.5) + r);
            double mor = m*oor;
            const int ir = r*(v.nz()+1);
            for (int z=0;z<v.nz();z++)
              f[Hr][cmp][z+ir] += c*
                ((f[Ep][cmp][z+ir+1]-f[Ep][cmp][z+ir]) - it(cmp,f[Ez],z+ir)*mor);
          }
      // Propogate Hp
      if (ma->C[Z][Hp] || ma->C[R][Hp])
        for (int r=rstart_0(v,m);r<v.nr();r++) {
            const int ir = r*(v.nz()+1);
            const int irp1 = (r+1)*(v.nz()+1);
            for (int z=0;z<v.nz();z++) {
              const double Czhp = (ma->C[Z][Hp])?ma->C[Z][Hp][z+ir]:0;
              const double Crhp = (ma->C[R][Hp])?ma->C[R][Hp][z+ir]:0;
              const double ooop_Czhp = (ma->Cdecay[Z][Hp][P]) ?
                ma->Cdecay[Z][Hp][P][z+ir]:1.0;
              const double dhpz = ooop_Czhp*(-c*(f[Er][cmp][z+ir+1]-f[Er][cmp][z+ir])
                                             - Czhp*f_p_pml[Hp][cmp][z+ir]);
              const double hpr = f[Hp][cmp][z+ir]-f_p_pml[Hp][cmp][z+ir];
              f_p_pml[Hp][cmp][z+ir] += dhpz;
              f[Hp][cmp][z+ir] += dhpz +
                ooop_Czhp*(c*(f[Ez][cmp][z+irp1]-f[Ez][cmp][z+ir]) - Crhp*hpr);
            }
          }
      else 
        for (int r=rstart_0(v,m);r<v.nr();r++) {
            const int ir = r*(v.nz()+1);
            const int irp1 = (r+1)*(v.nz()+1);
            for (int z=0;z<v.nz();z++)
              f[Hp][cmp][z+ir] += c*
                ((f[Ez][cmp][z+irp1]-f[Ez][cmp][z+ir])
                 - (f[Er][cmp][z+ir+1]-f[Er][cmp][z+ir]));
        }
      // Propogate Hz
      if (ma->C[R][Hz])
        for (int r=rstart_0(v,m);r<v.nr();r++) {
          double oorph = 1.0/((int)(v.origin.r()*v.a+0.5) + r+0.5);
          double morph = m*oorph;
          const int ir = r*(v.nz()+1);
          const int irp1 = (r+1)*(v.nz()+1);
          for (int z=1;z<=v.nz();z++) {
            const double Crhz = ma->C[R][Hz][z+ir];
            const double ooop_Crhz = ma->Cdecay[R][Hz][Z][z+ir];
            const double dhzr =
              ooop_Crhz*(-c*(f[Ep][cmp][z+irp1]*((int)(v.origin.r()*v.a+0.5) + r+1.)-
                             f[Ep][cmp][z+ir]*((int)(v.origin.r()*v.a+0.5) + r))*oorph
                         - Crhz*f_p_pml[Hz][cmp][z+ir]);
            f_p_pml[Hz][cmp][z+ir] += dhzr;
            f[Hz][cmp][z+ir] += dhzr + c*(it(cmp,f[Er],z+ir)*morph);
          }
        }
      else
        for (int r=rstart_0(v,m);r<v.nr();r++) {
          double oorph = 1.0/((int)(v.origin.r()*v.a+0.5) + r+0.5);
          double morph = m*oorph;
          const int ir = r*(v.nz()+1);
          const int irp1 = (r+1)*(v.nz()+1);
          for (int z=1;z<=v.nz();z++)
            f[Hz][cmp][z+ir] += c*
              (it(cmp,f[Er],z+ir)*morph
               - (f[Ep][cmp][z+irp1]*((int)(v.origin.r()*v.a+0.5) + r+1.)-
                  f[Ep][cmp][z+ir]*((int)(v.origin.r()*v.a+0.5) + r))*oorph);
        }
      // Deal with annoying r==0 boundary conditions...
      if (m == 0) {
        // Nothing needed for H.
      } else if (m == 1 && v.origin.r() == 0.0) {
        if (ma->C[Z][Hr])
          for (int z=0;z<v.nz();z++) {
            const double Czhr = ma->C[Z][Hr][z];
            const double ooop_Czhr = ma->Cdecay[Z][Hr][R][z];
            const double dhrp = c*(-it(cmp,f[Ez],z+(v.nz()+1))/* /1.0 */);
            const double hrz = f[Hr][cmp][z] - f_p_pml[Hr][cmp][z];
            f_p_pml[Hr][cmp][z] += dhrp;
            f[Hr][cmp][z] += dhrp +
              ooop_Czhr*(c*(f[Ep][cmp][z+1]-f[Ep][cmp][z]) - Czhr*hrz);
          }
        else
          for (int z=0;z<v.nz();z++)
            f[Hr][cmp][z] += c*
              ((f[Ep][cmp][z+1]-f[Ep][cmp][z]) - it(cmp,f[Ez],z+(v.nz()+1))/* /1.0 */);
      } else {
        for (int r=0;r<=v.nr() && (int)(v.origin.r()*v.a+0.5) + r < m;r++) {
          const int ir = r*(v.nz()+1);
          for (int z=0;z<=v.nz();z++) f[Hr][cmp][z+ir] = 0;
          if (f_p_pml[Hr][cmp])
            for (int z=0;z<=v.nz();z++) f_p_pml[Hr][cmp][z+ir] = 0;
        }
      }
    }
  } else {
    abort("Can't step H in these dimensions.");
  }
}