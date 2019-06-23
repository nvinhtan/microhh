/*
 * MicroHH
 * Copyright (c) 2011-2019 Chiel van Heerwaarden
 * Copyright (c) 2011-2019 Thijs Heus
 * Copyright (c) 2014-2019 Bart van Stratum
 *
 * This file is part of MicroHH
 *
 * MicroHH is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * MicroHH is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with MicroHH.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <iostream>
#include <cmath>
#include <vector>

#include "master.h"
#include "grid.h"
#include "fields.h"
#include "diff.h"
#include "stats.h"
#include "cross.h"
#include "thermo.h"
#include "thermo_moist_functions.h"

#include "constants.h"
#include "microphys.h"
#include "microphys_nsw6.h"
#include "microphys_2mom_warm.h"

// Constants, move out later.
namespace
{
    template<typename TF> constexpr TF ql_min = 1.e-7; // Threshold ql for calculating microphysical terms.
    template<typename TF> constexpr TF qi_min = 1.e-7; // Threshold qi for calculating microphysical terms.
    template<typename TF> constexpr TF qr_min = 1.e-12; // Threshold qr for calculating microphysical terms.
    template<typename TF> constexpr TF qs_min = 1.e-12; // Threshold qs for calculating microphysical terms.
    template<typename TF> constexpr TF qg_min = 1.e-12; // Threshold qg for calculating microphysical terms.

    template<typename TF> constexpr TF pi = M_PI; // Pi constant.
    template<typename TF> constexpr TF pi_2 = M_PI*M_PI; // Pi constant squared.

    template<typename TF> constexpr TF rho_w = 1.e3; // Density of water.
    template<typename TF> constexpr TF rho_s = 1.e2; // Density of snow.
    template<typename TF> constexpr TF rho_g = 4.e2; // Density of snow.

    template<typename TF> constexpr TF N_0r = 8.e6; // Intercept parameter rain (m-4).
    template<typename TF> constexpr TF N_0s = 3.e6; // Intercept parameter snow (m-4).
    template<typename TF> constexpr TF N_0g = 4.e6; // Intercept parameter graupel (m-4).

    template<typename TF> constexpr TF a_r = M_PI*rho_w<TF>/6.; // Empirical constant for m_r.
    template<typename TF> constexpr TF a_s = M_PI*rho_s<TF>/6.; // Empirical constant for m_s.
    template<typename TF> constexpr TF a_g = M_PI*rho_g<TF>/6.; // Empirical constant for m_g.

    template<typename TF> constexpr TF b_r = 3.; // Empirical constant for m_r.
    template<typename TF> constexpr TF b_s = 3.; // Empirical constant for m_s.
    template<typename TF> constexpr TF b_g = 3.; // Empirical constant for m_g.

    template<typename TF> constexpr TF c_r = 130.; // Empirical constant for v_r.
    template<typename TF> constexpr TF c_s = 4.84; // Empirical constant for v_s.
    template<typename TF> constexpr TF c_g = 82.5; // Empirical constant for v_g.

    template<typename TF> constexpr TF d_r = 0.5;  // Empirical constant for v_r.
    template<typename TF> constexpr TF d_s = 0.25; // Empirical constant for v_s.
    template<typename TF> constexpr TF d_g = 0.25; // Empirical constant for v_g.

    template<typename TF> constexpr TF C_i = 2006.; // Specific heat of solid water.
    template<typename TF> constexpr TF C_l = 4218.; // Specific heat of liquid water.
    template<typename TF> constexpr TF C_vd = 717.6; // Specific heat of dry air (constant volume).
    template<typename TF> constexpr TF C_vv = 1388.5; // Specific heat of water vapor (constant volume).

    template<typename TF> constexpr TF f_1r = 0.78; // First coefficient of ventilation factor for rain.
    template<typename TF> constexpr TF f_1s = 0.65; // First coefficient of ventilation factor for snow.
    template<typename TF> constexpr TF f_1g = 0.78; // First coefficient of ventilation factor for graupel.

    template<typename TF> constexpr TF f_2r = 0.27; // First coefficient of ventilation factor for rain.
    template<typename TF> constexpr TF f_2s = 0.39; // First coefficient of ventilation factor for snow.
    template<typename TF> constexpr TF f_2g = 0.27; // First coefficient of ventilation factor for graupel.

    template<typename TF> constexpr TF E_ri = 1.;  // Collection efficiency of ice for rain.
    template<typename TF> constexpr TF E_rw = 1.;  // Collection efficiency of rain for cloud water.
    template<typename TF> constexpr TF E_sw = 1.;  // Collection efficiency of snow for cloud water.
    template<typename TF> constexpr TF E_gw = 1.;  // Collection efficiency of graupel for cloud water.
    template<typename TF> constexpr TF E_gi = 0.1; // Collection efficiency of graupel for cloud ice.
    template<typename TF> constexpr TF E_sr = 1.;  // Collection efficiency of snow for rain.
    template<typename TF> constexpr TF E_gr = 1.;  // Collection efficiency of graupel for rain.

    template<typename TF> constexpr TF K_a = 2.43e-2;  // Thermal diffusion coefficient of air.
    template<typename TF> constexpr TF K_d = 2.26e-5;  // Diffusion coefficient of water vapor in air.

    template<typename TF> constexpr TF M_i = 4.19e-13; // Mass of one cloud ice particle.

    template<typename TF> constexpr TF gamma_sacr = 0.025;
    template<typename TF> constexpr TF gamma_saut = 0.025;
    template<typename TF> constexpr TF gamma_gacs = 0.09;
    template<typename TF> constexpr TF gamma_gaut = 0.09;

    template<typename TF> constexpr TF nu = 1.5e-5; // Kinematic viscosity of air.
}

namespace
{
    using namespace Constants;
    using namespace Thermo_moist_functions;
    using namespace Fast_math;
    using Micro_2mom_warm_functions::minmod;

    template<typename TF>
    void remove_negative_values(TF* const restrict field,
                                const int istart, const int jstart, const int kstart,
                                const int iend,   const int jend,   const int kend,
                                const int jj,     const int kk)
    {
        for (int k=kstart; k<kend; k++)
            for (int j=jstart; j<jend; j++)
                #pragma ivdep
                for (int i=istart; i<iend; i++)
                {
                    const int ijk = i + j*jj + k*kk;
                    field[ijk] = std::max(TF(0.), field[ijk]);
                }
    }

    template<typename TF>
    void zero_field(TF* const restrict field, const int ncells)
    {
        for (int n=0; n<ncells; n++)
            field[n] = TF(0.);
    }

    // Autoconversion.
    template<typename TF>
    void autoconversion(
            TF* const restrict qrt, TF* const restrict qst, TF* const restrict qgt,
            TF* const restrict qtt, TF* const restrict thlt,
            const TF* const restrict qr, const TF* const restrict qs, const TF* const restrict qg,
            const TF* const restrict qt, const TF* const restrict thl,
            const TF* const restrict ql, const TF* const restrict qi,
            const TF* const restrict rho, const TF* const restrict exner,
            const TF N_d,
            const int istart, const int jstart, const int kstart,
            const int iend, const int jend, const int kend,
            const int jj, const int kk)
    {
        // Tomita Eq. 51. N_d is converted to SI units (m-3 instead of cm-3).
        const TF D_d = TF(0.146) - TF(5.964e-2)*std::log(N_d / TF(2.e3 * 1.e6));

        for (int k=kstart; k<kend; k++)
            for (int j=jstart; j<jend; j++)
                #pragma ivdep
                for (int i=istart; i<iend; i++)
                {
                    const int ijk = i + j*jj + k*kk;

                    const bool has_liq = ql[ijk] > ql_min<TF>;
                    const bool has_ice = qi[ijk] > qi_min<TF>;
                    const bool has_snow = qs[ijk] > qs_min<TF>;

                    // Compute the T out of the known values of ql and qi, this saves memory and sat_adjust.
                    const TF T = exner[k]*thl[ijk] + Lv<TF>/cp<TF>*ql[ijk] + Ls<TF>/cp<TF>*qi[ijk];

                    constexpr TF q_icrt = TF(0.);
                    constexpr TF q_scrt = TF(6.e-4);

                    // Tomita Eq. 53
                    const TF beta_1 = std::min( TF(1.e-3), TF(1.e-3)*std::exp(gamma_saut<TF> * (T - T0<TF>)) );

                    // Tomita Eq. 54
                    const TF beta_2 = std::min( TF(1.e-3), TF(1.e-3)*std::exp(gamma_gaut<TF> * (T - T0<TF>)) );

                    // COMPUTE THE CONVERSION TERMS.
                    // Calculate the three autoconversion rates.
                    // Tomita Eq. 50
                    const TF P_raut = TF(16.7)/rho[k] * pow2(rho[k]*ql[ijk]) / (TF(5.) + TF(3.6e-5)*N_d/(D_d*rho[k]*ql[ijk]));

                    // Tomita Eq. 52
                    const TF P_saut = std::max(beta_1*(qi[ijk] - q_icrt), TF(0.));

                    // Tomita Eq. 54
                    const TF P_gaut = std::max(beta_2*(qs[ijk] - q_scrt), TF(0.));

                    // COMPUTE THE TENDENCIES.
                    // Cloud to rain.
                    if (has_liq)
                    {
                        qtt[ijk] -= P_raut;
                        qrt[ijk] += P_raut;
                        thlt[ijk] += Lv<TF> / (cp<TF> * exner[k]) * P_raut;
                    }

                    // Ice to snow.
                    if (has_ice)
                    {
                        qtt[ijk] -= P_saut;
                        qst[ijk] += P_saut;
                        thlt[ijk] += Ls<TF> / (cp<TF> * exner[k]) * P_saut;
                    }

                    // Snow to graupel.
                    if (has_snow)
                    {
                        qst[ijk] -= P_gaut;
                        qgt[ijk] += P_gaut;
                    }
                }
    }

    // Accretion.
    template<typename TF>
    void accretion_and_phase_changes(
            TF* const restrict qrt, TF* const restrict qst, TF* const restrict qgt,
            TF* const restrict qtt, TF* const restrict thlt,
            double& cfl_out,
            const TF* const restrict qr, const TF* const restrict qs, const TF* const restrict qg,
            const TF* const restrict qt, const TF* const restrict thl,
            const TF* const restrict ql, const TF* const restrict qi,
            const TF* const restrict rho, const TF* const restrict exner, const TF* const restrict p,
            const TF* const restrict dzi, const TF* const restrict dzhi,
            const TF dt,
            const int istart, const int jstart, const int kstart,
            const int iend, const int jend, const int kend,
            const int jj, const int kk)
    {
        TF cfl = TF(0.);

        for (int k=kstart; k<kend; ++k)
        {
            const TF rho0_rho_sqrt = std::sqrt(rho[kstart]/rho[k]);

            // Part of Tomita Eq. 29
            const TF fac_iacr =
                pi_2<TF> * E_ri<TF> * N_0r<TF> * c_r<TF> * rho_w<TF> * std::tgamma(TF(6.) + d_r<TF>)
                / (TF(24.) * M_i<TF>)
                * rho0_rho_sqrt;

            // Part of Tomita Eq. 32
            const TF fac_raci =
                pi<TF> * E_ri<TF> * N_0r<TF> * c_r<TF> * std::tgamma(TF(3.) + d_r<TF>)
                / TF(4.)
                * rho0_rho_sqrt;

            // Part of Tomita Eq. 34
            const TF fac_racw =
                pi<TF> * E_rw<TF> * N_0r<TF> * c_r<TF> * std::tgamma(TF(3.) + d_r<TF>)
                / TF(4.)
                * rho0_rho_sqrt;

            // Part of Tomita Eq. 35
            const TF fac_sacw =
                pi<TF> * E_sw<TF> * N_0s<TF> * c_s<TF> * std::tgamma(TF(3.) + d_s<TF>)
                / TF(4.)
                * rho0_rho_sqrt;

            // Part of Tomita Eq. 36 (E_si is temperature dependent and missing therefore here).
            const TF fac_saci =
                pi<TF> * N_0s<TF> * c_s<TF> * std::tgamma(TF(3.) + d_s<TF>)
                / TF(4.)
                * rho0_rho_sqrt;

            // Part of Tomita Eq. 37
            const TF fac_gacw =
                pi<TF> * E_gw<TF> * N_0g<TF> * c_g<TF> * std::tgamma(TF(3.) + d_g<TF>)
                / TF(4.)
                * rho0_rho_sqrt;

            // Part of Tomita Eq. 38
            const TF fac_gaci =
                pi<TF> * E_gi<TF> * N_0g<TF> * c_g<TF> * std::tgamma(TF(3.) + d_g<TF>)
                / TF(4.)
                * rho0_rho_sqrt;

            for (int j=jstart; j<jend; ++j)
                #pragma ivdep
                for (int i=istart; i<iend; ++i)
                {
                    const int ijk = i + j*jj + k*kk;

                    // Compute the T out of the known values of ql and qi, this saves memory and sat_adjust.
                    const TF T = exner[k]*thl[ijk] + Lv<TF>/cp<TF>*ql[ijk] + Ls<TF>/cp<TF>*qi[ijk];

                    const bool has_liq = ql[ijk] > ql_min<TF>;
                    const bool has_ice = qi[ijk] > qi_min<TF>;
                    const bool has_rain = qr[ijk] > qr_min<TF>;
                    const bool has_snow = qs[ijk] > qs_min<TF>;
                    const bool has_graupel = qg[ijk] > qg_min<TF>;

                    // Tomita Eq. 27
                    const TF lambda_r = std::pow(
                            a_r<TF> * N_0r<TF> * std::tgamma(b_r<TF> + TF(1.))
                            / (rho[k] * qr[ijk]),
                            TF(1.) / (b_r<TF> + TF(1.)) );

                    const TF lambda_s = std::pow(
                            a_s<TF> * N_0r<TF> * std::tgamma(b_s<TF> + TF(1.))
                            / (rho[k] * qs[ijk]),
                            TF(1.) / (b_s<TF> + TF(1.)) );

                    const TF lambda_g = std::pow(
                            a_g<TF> * N_0r<TF> * std::tgamma(b_g<TF> + TF(1.))
                            / (rho[k] * qg[ijk]),
                            TF(1.) / (b_g<TF> + TF(1.)) );

                    // Tomita Eq. 28
                    const TF V_Tr =
                        c_r<TF> * rho0_rho_sqrt
                        * std::tgamma(b_r<TF> + d_r<TF> + TF(1.)) / std::tgamma(b_r<TF> + TF(1.))
                        * std::pow(lambda_r, -d_r<TF>);

                    const TF V_Ts =
                        c_s<TF> * rho0_rho_sqrt
                        * std::tgamma(b_s<TF> + d_s<TF> + TF(1.)) / std::tgamma(b_s<TF> + TF(1.))
                        * std::pow(lambda_s, -d_s<TF>);

                    const TF V_Tg =
                        c_g<TF> * rho0_rho_sqrt
                        * std::tgamma(b_g<TF> + d_g<TF> + TF(1.)) / std::tgamma(b_g<TF> + TF(1.))
                        * std::pow(lambda_g, -d_g<TF>);

                    cfl = (has_rain   ) ? std::max(V_Tr * dt * dzi[k], cfl) : cfl;
                    cfl = (has_snow   ) ? std::max(V_Ts * dt * dzi[k], cfl) : cfl;
                    cfl = (has_graupel) ? std::max(V_Tg * dt * dzi[k], cfl) : cfl;

                    // COMPUTE THE CONVERSION TERMS.
                    // Tomita Eq. 29
                    const TF P_iacr = fac_iacr / std::pow(lambda_r, TF(6.) + d_r<TF>) * qi[ijk];

                    // Tomita Eq. 30
                    const TF delta_1 = TF(qr[ijk] >= TF(1.e-4));

                    // Tomita Eq. 31
                    const TF P_iacr_s = (TF(1.) - delta_1) * P_iacr;
                    const TF P_iacr_g = delta_1 * P_iacr;

                    // Tomita Eq. 32
                    const TF P_raci = fac_raci / std::pow(lambda_r, TF(3.) + d_r<TF>) * qi[ijk];

                    // Tomita Eq. 33
                    const TF P_raci_s = (TF(1.) - delta_1) * P_raci;
                    const TF P_raci_g = delta_1 * P_raci;

                    // Tomita Eq. 34, 35
                    const TF P_racw = fac_racw / std::pow(lambda_r, TF(3.) + d_r<TF>) * ql[ijk];
                    const TF P_sacw = fac_sacw / std::pow(lambda_s, TF(3.) + d_s<TF>) * ql[ijk];

                    // Tomita Eq. 39
                    const TF E_si = std::exp(gamma_sacr<TF> * (T - T0<TF>));

                    // Tomita Eq. 36 - 38
                    const TF P_saci = fac_saci * E_si / std::pow(lambda_s, TF(3.) + d_s<TF>) * qi[ijk];
                    const TF P_gacw = fac_gacw / std::pow(lambda_g, TF(3.) + d_g<TF>) * ql[ijk];
                    const TF P_gaci = fac_gaci / std::pow(lambda_g, TF(3.) + d_g<TF>) * qi[ijk];

                    // Accretion of falling hydrometeors.
                    // Tomita Eq. 42
                    const TF delta_2 = TF(1.) - TF( (qr[ijk] >= TF(1.e-4)) | (qs[ijk] >= TF(1.e-4)) );

                    // Tomita Eq. 41
                    const TF P_racs = (TF(1.) - delta_2)
                        * pi<TF> * a_s<TF> * std::abs(V_Tr - V_Ts) * E_sr<TF> * N_0s<TF> * N_0r<TF> / (TF(4.)*rho[k])
                        * (          std::tgamma(b_s<TF> + TF(3.)) * std::tgamma(TF(1.)) / ( std::pow(lambda_s, b_s<TF> + TF(3.)) * lambda_r )
                          + TF(2.) * std::tgamma(b_s<TF> + TF(2.)) * std::tgamma(TF(2.)) / ( std::pow(lambda_s, b_s<TF> + TF(2.)) * pow2(lambda_r) )
                          +          std::tgamma(b_s<TF> + TF(1.)) * std::tgamma(TF(3.)) / ( std::pow(lambda_s, b_s<TF> + TF(1.)) * pow3(lambda_r) ) );

                    // Tomita Eq. 41
                    const TF P_sacr =
                          pi<TF> * a_r<TF> * std::abs(V_Ts - V_Tr) * E_sr<TF> * N_0r<TF> * N_0s<TF> / (TF(4.)*rho[k])
                        * (          std::tgamma(b_r<TF> + TF(1.)) * std::tgamma(TF(3.)) / ( std::pow(lambda_r, b_r<TF> + TF(1.)) * pow3(lambda_s) )
                          + TF(2.) * std::tgamma(b_r<TF> + TF(2.)) * std::tgamma(TF(2.)) / ( std::pow(lambda_r, b_r<TF> + TF(2.)) * pow2(lambda_s) )
                          +          std::tgamma(b_r<TF> + TF(3.)) * std::tgamma(TF(1.)) / ( std::pow(lambda_r, b_r<TF> + TF(3.)) * lambda_s ) );

                    // Tomita Eq. 43
                    const TF P_sacr_g = (TF(1.) - delta_2) * P_sacr;
                    const TF P_sacr_s = delta_2 * P_sacr;

                    // Tomita Eq. 49
                    const TF E_gs = std::min( TF(1.), std::exp(gamma_gacs<TF> * (T - T0<TF>)) );

                    // Tomita Eq. 47
                    const TF P_gacr =
                          pi<TF> * a_r<TF> * std::abs(V_Tg - V_Tr) * E_gr<TF> * N_0g<TF> * N_0r<TF> / (TF(4.)*rho[k])
                        * (          std::tgamma(b_r<TF> + TF(1.)) * std::tgamma(TF(3.)) / ( std::pow(lambda_r, b_r<TF> + TF(1.)) * pow3(lambda_g) )
                          + TF(2.) * std::tgamma(b_r<TF> + TF(2.)) * std::tgamma(TF(2.)) / ( std::pow(lambda_r, b_r<TF> + TF(2.)) * pow2(lambda_g) )
                          +          std::tgamma(b_r<TF> + TF(3.)) * std::tgamma(TF(1.)) / ( std::pow(lambda_r, b_r<TF> + TF(3.)) * lambda_g ) );

                    // Tomita Eq. 48
                    const TF P_gacs =
                          pi<TF> * a_s<TF> * std::abs(V_Tg - V_Ts) * E_gs * N_0g<TF> * N_0s<TF> / (TF(4.)*rho[k])
                        * (          std::tgamma(b_s<TF> + TF(1.)) * std::tgamma(TF(3.)) / ( std::pow(lambda_r, b_s<TF> + TF(1.)) * pow3(lambda_g) )
                          + TF(2.) * std::tgamma(b_s<TF> + TF(2.)) * std::tgamma(TF(2.)) / ( std::pow(lambda_r, b_s<TF> + TF(2.)) * pow2(lambda_g) )
                          +          std::tgamma(b_s<TF> + TF(3.)) * std::tgamma(TF(1.)) / ( std::pow(lambda_r, b_s<TF> + TF(3.)) * lambda_g ) );

                    // Compute evaporation and deposition
                    // Tomita Eq. 57
                    const TF G_w = TF(1.) / (
                        Lv<TF> / (K_a<TF> * T) * (Lv<TF>/(Rv<TF> * T) - TF(1.))
                        + Rv<TF>*T / (K_d<TF> * esat_liq(T)) );

                    // Tomita Eq. 62
                    const TF G_i = TF(1.) / (
                        Ls<TF> / (K_a<TF> * T) * (Ls<TF>/(Rv<TF> * T) - TF(1.))
                        + Rv<TF>*T / (K_d<TF> * esat_ice(T)) );

                    const TF S_w = (qt[ijk] - ql[ijk] - qi[ijk]) / qsat_liq(p[k], T);
                    const TF S_i = (qt[ijk] - ql[ijk] - qi[ijk]) / qsat_ice(p[k], T);

                    // Tomita Eq. 63
                    const TF delta_3 = TF( (S_i - TF(1.)) <= TF(0.) );

                    // Tomita Eq. 59
                    const TF P_revp = 
                        - TF(2.)*pi<TF> * N_0r<TF> * (std::min(S_w, TF(1.)) - TF(1.)) * G_w / rho[k]
                        * ( f_1r<TF> * std::tgamma(TF(2.)) / pow2(lambda_r)
                          + f_2r<TF> * std::sqrt(c_r<TF> * rho0_rho_sqrt / nu<TF>)
                          * std::tgamma( TF(0.5) * (TF(5.) + d_r<TF>) )
                          / std::pow(lambda_r, TF(0.5) * (TF(5.) + d_r<TF>)) );

                    // Tomita Eq. 60
                    const TF P_sdep_ssub = 
                        TF(2.)*pi<TF> * N_0s<TF> * (S_i - TF(1.)) * G_i / rho[k]
                        * ( f_1s<TF> * std::tgamma(TF(2.)) / pow2(lambda_s)
                          + f_2s<TF> * std::sqrt(c_s<TF> * rho0_rho_sqrt / nu<TF>)
                          * std::tgamma( TF(0.5) * (TF(5.) + d_s<TF>) )
                          / std::pow(lambda_s, TF(0.5) * (TF(5.) + d_s<TF>)) );

                    // Tomita Eq. 51
                    const TF P_gdep_gsub = 
                        TF(2.)*pi<TF> * N_0g<TF> * (S_i - TF(1.)) * G_i / rho[k]
                        * ( f_1g<TF> * std::tgamma(TF(2.)) / pow2(lambda_g)
                          + f_2g<TF> * std::sqrt(c_g<TF> * rho0_rho_sqrt / nu<TF>)
                          * std::tgamma( TF(0.5) * (TF(5.) + d_g<TF>) )
                          / std::pow(lambda_g, TF(0.5) * (TF(5.) + d_g<TF>)) );

                    // Tomita Eq. 64
                    const TF P_sdep = (delta_3 - TF(1.)) * P_sdep_ssub;
                    const TF P_gdep = (delta_3 - TF(1.)) * P_gdep_gsub;

                    // Tomita Eq. 65
                    const TF P_ssub = delta_3 * P_sdep_ssub;
                    const TF P_gsub = delta_3 * P_gdep_gsub;

                    // Freezing and melting
                    // Tomita Eq. 67, 68 combined.
                    const TF P_smlt = 
                        TF(2.)*pi<TF> * K_a<TF> * (T - T0<TF>) * N_0s<TF> / (rho[k]*Lf<TF>)
                        * ( f_1s<TF> * std::tgamma(TF(2.)) / pow2(lambda_s)
                          + f_2s<TF> * std::sqrt(c_s<TF> * rho0_rho_sqrt / nu<TF>)
                          * std::tgamma( TF(0.5) * (TF(5.) + d_s<TF>) )
                          / std::pow(lambda_s, TF(0.5) * (TF(5.) + d_s<TF>)) )
                        + C_l<TF> * (T - T0<TF>) / Lf<TF> * (P_sacw + P_sacr);

                    // Tomita Eq. 69
                    const TF P_gmlt = 
                        TF(2.)*pi<TF> * K_a<TF> * (T - T0<TF>) * N_0g<TF> / (rho[k]*Lf<TF>)
                        * ( f_1g<TF> * std::tgamma(TF(2.)) / pow2(lambda_g)
                          + f_2g<TF> * std::sqrt(c_g<TF> * rho0_rho_sqrt / nu<TF>)
                          * std::tgamma( TF(0.5) * (TF(5.) + d_g<TF>) )
                          / std::pow(lambda_g, TF(0.5) * (TF(5.) + d_g<TF>)) )
                        + C_l<TF> * (T - T0<TF>) / Lf<TF> * (P_gacw + P_gacr);

                    // Tomita Eq. 70
                    constexpr TF A_prime = TF(0.66);
                    constexpr TF B_prime = TF(100.);
                    const TF P_gfrz =
                        TF(20.) * pi_2<TF> * B_prime * N_0r<TF> * rho_w<TF> / rho[k]
                        * (std::exp(A_prime * (T0<TF> - T)) - TF(1.)) / pow7(lambda_r);

                    // COMPUTE THE TENDENCIES.
                    // Flag the sign of the absolute temperature.
                    const TF T_pos = TF(T >= T0<TF>);
                    const TF T_neg = TF(1.) - T_pos;

                    // WARM PROCESSES.
                    // Cloud to rain.
                    if (has_liq)
                    {
                        qtt[ijk] -= P_racw + P_sacw * T_pos;
                        qrt[ijk] += P_racw + P_sacw * T_pos;
                        thlt[ijk] += Lv<TF> / (cp<TF> * exner[k]) * (P_racw + P_sacw * T_pos);
                    }

                    // Rain to vapor.
                    if (has_rain)
                    {
                        qrt[ijk] -= P_revp;
                        qtt[ijk] += P_revp;
                        thlt[ijk] -= Lv<TF> / (cp<TF> * exner[k]) * P_revp;
                    }

                    // COLD PROCESSES.
                    // Cloud to graupel.
                    if (has_liq)
                    {
                        qtt[ijk] -= P_gacw;
                        qgt[ijk] += P_gacw;
                        thlt[ijk] += Ls<TF> / (cp<TF> * exner[k]) * P_gacw;
                    }

                    // Cloud to snow.
                    if (has_liq)
                    {
                        qtt[ijk] -= P_sacw * T_neg;
                        qst[ijk] += P_sacw * T_neg;
                        thlt[ijk] += Ls<TF> / (cp<TF> * exner[k]) * (P_sacw * T_neg);
                    }

                    // Ice to snow.
                    if (has_ice)
                    {
                        qtt[ijk] -= P_raci_s + P_saci;
                        qst[ijk] += P_raci_s + P_saci;
                        thlt[ijk] += Ls<TF> / (cp<TF> * exner[k]) * (P_raci_s + P_saci);
                    }

                    // Ice to graupel.
                    if (has_ice)
                    {
                        qtt[ijk] -= P_raci_g + P_gaci;
                        qgt[ijk] += P_raci_g + P_gaci;
                        thlt[ijk] += Ls<TF> / (cp<TF> * exner[k]) * (P_raci_g + P_gaci);
                    }

                    // Rain to graupel.
                    if (has_rain)
                    {
                        qrt[ijk] -= P_gacr + P_iacr_g + P_sacr_g * T_neg + P_gfrz * T_neg;
                        qgt[ijk] += P_gacr + P_iacr_g + P_sacr_g * T_neg + P_gfrz * T_neg;
                        thlt[ijk] += Lf<TF> / (cp<TF> * exner[k]) * (P_gacr + P_iacr_g + P_sacr_g * T_neg + P_gfrz * T_neg);
                    }

                    // Rain to snow.
                    if (has_rain)
                    {
                        qrt[ijk] -= P_sacr_s * T_neg + P_iacr_s;
                        qst[ijk] += P_sacr_s * T_neg + P_iacr_s;
                        thlt[ijk] += Lf<TF> / (cp<TF> * exner[k]) * (P_gacr + P_iacr_g + P_sacr_g * T_neg + P_gfrz * T_neg);
                    }

                    // Snow to rain.
                    if (has_snow)
                    {
                        qst[ijk] -= P_smlt * T_pos;
                        qrt[ijk] += P_smlt * T_pos;
                        thlt[ijk] -= Lf<TF> / (cp<TF> * exner[k]) * P_smlt * T_pos;
                    }

                    // Snow to graupel.
                    if (has_snow)
                    {
                        qst[ijk] -= P_gacs + P_racs;
                        qgt[ijk] += P_gacs + P_racs;
                    }

                    // Snow to vapor.
                    if (has_snow)
                    {
                        qst[ijk] -= P_sdep + P_ssub;
                        qtt[ijk] += P_sdep + P_ssub;
                        thlt[ijk] -= Ls<TF> / (cp<TF> * exner[k]) * (P_sdep + P_ssub);
                    }

                    // Graupel to rain.
                    if (has_graupel)
                    {
                        qgt[ijk] -= P_gmlt * T_pos;
                        qrt[ijk] += P_gmlt * T_pos;
                        thlt[ijk] -= Lf<TF> / (cp<TF> * exner[k]) * (P_gmlt * T_pos);
                    }

                    // Graupel to vapor.
                    if (has_graupel)
                    {
                        qgt[ijk] -= P_gdep + P_gsub;
                        qtt[ijk] += P_gdep + P_gsub;
                        thlt[ijk] -= Ls<TF> / (cp<TF> * exner[k]) * (P_gdep + P_gsub);
                    }
                }
        }

        cfl_out = cfl;
    }

    // Bergeron.
    template<typename TF>
    void bergeron(
            TF* const restrict qst,
            TF* const restrict qtt, TF* const restrict thlt,
            const TF* const restrict ql, const TF* const restrict qi,
            const TF* const restrict rho, const TF* const restrict exner,
            const TF delta_t,
            const int istart, const int jstart, const int kstart,
            const int iend, const int jend, const int kend,
            const int jj, const int kk)
    {
        constexpr TF m_i40 = TF(2.46e-10);
        constexpr TF m_i50 = TF(4.8e-10);
        constexpr TF R_i50 = TF(5.e-5);

        // constexpr TF a1 = 
        // constexpr TF a2 = 

        // constexpr TF delta_t1 =
        //     ( std::pow(m_i50, TF(1.) - a_2) - std::pow(m_i40, TF(1.) - a_2) )
        //     / (a_1 * (TF(1.) - a_2));

        for (int k=kstart; k<kend; k++)
            for (int j=jstart; j<jend; j++)
                #pragma ivdep
                for (int i=istart; i<iend; i++)
                {
                    const int ijk = i + j*jj + k*kk;
                    // To be filled in.
                }
    }

    // Sedimentation from Stevens and Seifert (2008)
    template<typename TF>
    void sedimentation_ss08(
            TF* const restrict qct, TF* const restrict rc_bot,
            TF* const restrict w_qc, TF* const restrict c_qc,
            TF* const restrict slope_qc, TF* const restrict flux_qc,
            const TF* const restrict qc,
            const TF* const restrict rho,
            const TF* const restrict dzi, const TF* const restrict dz,
            const double dt,
            const TF a_c, const TF b_c, const TF c_c, const TF d_c, const TF N_0c,
            const TF qc_min,
            const int istart, const int jstart, const int kstart,
            const int iend, const int jend, const int kend,
            const int jj, const int kk)
    {
        // 1. Calculate sedimentation velocity at cell center
        for (int k=kstart; k<kend; ++k)
        {
            const TF rho0_rho_sqrt = std::sqrt(rho[kstart]/rho[k]);

            for (int j=jstart; j<jend; ++j)
                #pragma ivdep
                for (int i=istart; i<iend; ++i)
                {
                    const int ijk = i + j*jj + k*kk;

                    if (qc[ijk] > qc_min)
                    {
                        const TF lambda_c = std::pow(
                            a_c * N_0c * std::tgamma(b_c + TF(1.))
                            / (rho[k] * qc[ijk]),
                            TF(1.) / (b_c + TF(1.)) );

                        const TF V_T =
                            c_c * rho0_rho_sqrt
                            * std::tgamma(b_c + d_c + TF(1.)) / std::tgamma(b_c + TF(1.))
                            * std::pow(lambda_c, -d_c);

                        w_qc[ijk] = V_T;
                    }
                    else
                        w_qc[ijk] = TF(0.);
                }
        }

        // 1.1 Set one ghost cell to zero
        for (int j=jstart; j<jend; ++j)
            for (int i=istart; i<iend; ++i)
            {
                const int ijk_bot = i + j*jj + (kstart-1)*kk;
                const int ijk_top = i + j*jj + (kend    )*kk;
                w_qc[ijk_bot] = w_qc[ijk_bot+kk];
                w_qc[ijk_top] = TF(0.);
            }

        // 2. Calculate CFL number using interpolated sedimentation velocity
        for (int k=kstart; k<kend; ++k)
            for (int j=jstart; j<jend; ++j)
                #pragma ivdep
                for (int i=istart; i<iend; ++i)
                {
                    const int ijk = i + j*jj + k*kk;
                    c_qc[ijk] = TF(0.25) * (w_qc[ijk-kk] + TF(2.)*w_qc[ijk] + w_qc[ijk+kk]) * dzi[k] * dt;
                }

        // 3. Calculate slopes
        for (int k=kstart; k<kend; ++k)
            for (int j=jstart; j<jend; ++j)
                #pragma ivdep
                for (int i=istart; i<iend; ++i)
                {
                    const int ijk = i + j*jj + k*kk;
                    slope_qc[ijk] = minmod(qc[ijk]-qc[ijk-kk], qc[ijk+kk]-qc[ijk]);
                }

        // Calculate flux
        // Set the fluxes at the top of the domain (kend) to zero
        for (int j=jstart; j<jend; ++j)
            for (int i=istart; i<iend; ++i)
            {
                const int ijk = i + j*jj + kend*kk;
                flux_qc[ijk] = TF(0.);
            }

        for (int k=kend-1; k>kstart-1; --k)
            for (int j=jstart; j<jend; ++j)
                #pragma ivdep
                for (int i=istart; i<iend; ++i)
                {
                    const int ijk = i + j*jj + k*kk;

                    // q_rain
                    int kc = k; // current grid level
                    TF ftot = TF(0.); // cumulative 'flux' (kg m-2)
                    TF dzz = TF(0.); // distance from zh[k]
                    TF cc = std::min(TF(1.), c_qc[ijk]);
                    while (cc > 0 && kc < kend)
                    {
                        const int ijkc = i + j*jj+ kc*kk;

                        ftot += rho[kc] * (qc[ijkc] + TF(0.5) * slope_qc[ijkc] * (TF(1.)-cc)) * cc * dz[kc];

                        dzz += dz[kc];
                        kc += 1;
                        cc = std::min(TF(1.), c_qc[ijkc] - dzz*dzi[kc]);
                    }

                    // Given flux at top, limit bottom flux such that the total rain content stays >= 0.
                    ftot = std::min(ftot, rho[k] * dz[k] * qc[ijk] - flux_qc[ijk+kk] * TF(dt));
                    flux_qc[ijk] = -ftot / dt;
                }

        // Calculate tendency
        for (int k=kstart; k<kend; ++k)
            for (int j=jstart; j<jend; ++j)
                #pragma ivdep
                for (int i=istart; i<iend; ++i)
                {
                    const int ijk = i + j*jj + k*kk;
                    qct[ijk] += -(flux_qc[ijk+kk] - flux_qc[ijk]) / rho[k] * dzi[k];
                }

        // Store surface sedimentation flux
        // Sedimentation flux is already multiplied with density (see flux div. calculation), so
        // the resulting flux is in kg m-2 s-1, with rho_water = 1000 kg/m3 this equals a rain rate in mm s-1
        for (int j=jstart; j<jend; ++j)
            for (int i=istart; i<iend; ++i)
            {
                const int ij = i + j*jj;
                const int ijk = i + j*jj + kstart*kk;

                rc_bot[ij] = -flux_qc[ijk];
            }
    }
}

template<typename TF>
Microphys_nsw6<TF>::Microphys_nsw6(Master& masterin, Grid<TF>& gridin, Fields<TF>& fieldsin, Input& inputin) :
    Microphys<TF>(masterin, gridin, fieldsin, inputin)
{
    auto& gd = grid.get_grid_data();
    swmicrophys = Microphys_type::Nsw6;

    // Read microphysics switches and settings
    // swmicrobudget = inputin.get_item<bool>("micro", "swmicrobudget", "", false);
    cfl_max = inputin.get_item<TF>("micro", "cflmax", "", 2.);
    cfl = 0;

    N_d = inputin.get_item<TF>("micro", "Nd", "", 50.e6); // CvH: 50 cm-3 do we need conversion, or do we stick with Tomita?

    // Initialize the qr (rain water specific humidity) and nr (droplot number concentration) fields
    fields.init_prognostic_field("qr", "Rain water specific humidity", "kg kg-1", gd.sloc);
    fields.init_prognostic_field("qs", "Snow specific humidity", "kg kg-1", gd.sloc);
    fields.init_prognostic_field("qg", "Graupel specific humidity", "kg kg-1", gd.sloc);

    // Load the viscosity for both fields.
    fields.sp.at("qr")->visc = inputin.get_item<TF>("fields", "svisc", "qr");
    fields.sp.at("qg")->visc = inputin.get_item<TF>("fields", "svisc", "qg");
    fields.sp.at("qs")->visc = inputin.get_item<TF>("fields", "svisc", "qs");
}

template<typename TF>
Microphys_nsw6<TF>::~Microphys_nsw6()
{
}

template<typename TF>
void Microphys_nsw6<TF>::init()
{
    auto& gd = grid.get_grid_data();

    rr_bot.resize(gd.ijcells);
    rs_bot.resize(gd.ijcells);
    rg_bot.resize(gd.ijcells);
}

template<typename TF>
void Microphys_nsw6<TF>::create(Input& inputin, Netcdf_handle& input_nc, Stats<TF>& stats, Cross<TF>& cross, Dump<TF>& dump)
{
    const std::string group_name = "thermo";

    // Add variables to the statistics
    if (stats.get_switch())
    {
        // Time series
        stats.add_time_series("rr", "Mean surface rain rate", "kg m-2 s-1", group_name);
        stats.add_time_series("rs", "Mean surface snow rate", "kg m-2 s-1", group_name);
        stats.add_time_series("rg", "Mean surface graupel rate", "kg m-2 s-1", group_name);

        stats.add_tendency(*fields.st.at("thl"), "z", tend_name, tend_longname);
        stats.add_tendency(*fields.st.at("qt") , "z", tend_name, tend_longname);
        stats.add_tendency(*fields.st.at("qr") , "z", tend_name, tend_longname);
        stats.add_tendency(*fields.st.at("qs") , "z", tend_name, tend_longname);
        stats.add_tendency(*fields.st.at("qg") , "z", tend_name, tend_longname);
    }
}

#ifndef USECUDA
template<typename TF>
void Microphys_nsw6<TF>::exec(Thermo<TF>& thermo, const double dt, Stats<TF>& stats)
{
    auto& gd = grid.get_grid_data();

    // Get liquid water, ice and pressure variables before starting.
    auto ql = fields.get_tmp();
    auto qi = fields.get_tmp();

    thermo.get_thermo_field(*ql, "ql", false, false);
    thermo.get_thermo_field(*qi, "qi", false, false);

    const std::vector<TF>& p = thermo.get_p_vector();
    const std::vector<TF>& exner = thermo.get_exner_vector();

    autoconversion(
            fields.st.at("qr")->fld.data(), fields.st.at("qs")->fld.data(), fields.st.at("qg")->fld.data(),
            fields.st.at("qt")->fld.data(), fields.st.at("thl")->fld.data(),
            fields.sp.at("qr")->fld.data(), fields.sp.at("qs")->fld.data(), fields.sp.at("qg")->fld.data(),
            fields.sp.at("qt")->fld.data(), fields.sp.at("thl")->fld.data(),
            ql->fld.data(), qi->fld.data(),
            fields.rhoref.data(), exner.data(),
            this->N_d,
            gd.istart, gd.jstart, gd.kstart,
            gd.iend, gd.jend, gd.kend,
            gd.icells, gd.ijcells);

    accretion_and_phase_changes(
            fields.st.at("qr")->fld.data(), fields.st.at("qs")->fld.data(), fields.st.at("qg")->fld.data(),
            fields.st.at("qt")->fld.data(), fields.st.at("thl")->fld.data(),
            this->cfl,
            fields.sp.at("qr")->fld.data(), fields.sp.at("qs")->fld.data(), fields.sp.at("qg")->fld.data(),
            fields.sp.at("qt")->fld.data(), fields.sp.at("thl")->fld.data(),
            ql->fld.data(), qi->fld.data(),
            fields.rhoref.data(), exner.data(), p.data(),
            gd.dzi.data(), gd.dzhi.data(),
            TF(dt),
            gd.istart, gd.jstart, gd.kstart,
            gd.iend, gd.jend, gd.kend,
            gd.icells, gd.ijcells);

    /*
    bergeron(
            fields.st.at("qs")->fld.data(),
            fields.st.at("qt")->fld.data(), fields.st.at("thl")->fld.data(),
            ql->fld.data(), qi->fld.data(),
            fields.rhoref.data(), exner.data(),
            TF(dt),
            gd.istart, gd.jstart, gd.kstart,
            gd.iend, gd.jend, gd.kend,
            gd.icells, gd.ijcells);
            */

    fields.release_tmp(ql);
    fields.release_tmp(qi);

    auto tmp1 = fields.get_tmp();
    auto tmp2 = fields.get_tmp();
    auto tmp3 = fields.get_tmp();
    auto tmp4 = fields.get_tmp();

    // Falling rain.
    sedimentation_ss08(
            fields.st.at("qr")->fld.data(), rr_bot.data(),
            tmp1->fld.data(), tmp2->fld.data(),
            tmp3->fld.data(), tmp4->fld.data(),
            fields.sp.at("qr")->fld.data(),
            fields.rhoref.data(),
            gd.dzi.data(), gd.dz.data(),
            dt,
            a_r<TF>, b_r<TF>, c_r<TF>, d_r<TF>, N_0r<TF>,
            qr_min<TF>,
            gd.istart, gd.jstart, gd.kstart,
            gd.iend, gd.jend, gd.kend,
            gd.icells, gd.ijcells);

    // Falling snow.
    sedimentation_ss08(
            fields.st.at("qs")->fld.data(), rs_bot.data(),
            tmp1->fld.data(), tmp2->fld.data(),
            tmp3->fld.data(), tmp4->fld.data(),
            fields.sp.at("qs")->fld.data(),
            fields.rhoref.data(),
            gd.dzi.data(), gd.dz.data(),
            dt,
            a_s<TF>, b_s<TF>, c_s<TF>, d_s<TF>, N_0s<TF>,
            qs_min<TF>,
            gd.istart, gd.jstart, gd.kstart,
            gd.iend, gd.jend, gd.kend,
            gd.icells, gd.ijcells);

    // Falling graupel.
    sedimentation_ss08(
            fields.st.at("qg")->fld.data(), rg_bot.data(),
            tmp1->fld.data(), tmp2->fld.data(),
            tmp3->fld.data(), tmp4->fld.data(),
            fields.sp.at("qg")->fld.data(),
            fields.rhoref.data(),
            gd.dzi.data(), gd.dz.data(),
            dt,
            a_g<TF>, b_g<TF>, c_g<TF>, d_g<TF>, N_0g<TF>,
            qg_min<TF>,
            gd.istart, gd.jstart, gd.kstart,
            gd.iend, gd.jend, gd.kend,
            gd.icells, gd.ijcells);

    fields.release_tmp(tmp1);
    fields.release_tmp(tmp2);
    fields.release_tmp(tmp3);
    fields.release_tmp(tmp4);

    stats.calc_tend(*fields.st.at("thl"), tend_name);
    stats.calc_tend(*fields.st.at("qt" ), tend_name);
    stats.calc_tend(*fields.st.at("qr" ), tend_name);
    stats.calc_tend(*fields.st.at("qg" ), tend_name);
    stats.calc_tend(*fields.st.at("qs" ), tend_name);
}
#endif

template<typename TF>
void Microphys_nsw6<TF>::exec_stats(Stats<TF>& stats, Thermo<TF>& thermo, const double dt)
{
    // Time series
    const TF no_offset = 0.;
    stats.calc_stats_2d("rr", rr_bot, no_offset);
}

template<typename TF>
void Microphys_nsw6<TF>::exec_cross(Cross<TF>& cross, unsigned long iotime)
{
}

#ifndef USECUDA
template<typename TF>
unsigned long Microphys_nsw6<TF>::get_time_limit(unsigned long idt, const double dt)
{
    // Prevent zero division.
    this->cfl = std::max(this->cfl, 1.e-5);

    if (this->cfl > this->cfl_max)
        std::cout << "CvH: " << this->cfl << std::endl;
    return idt * this->cfl_max / this->cfl;
}
#endif

template<typename TF>
bool Microphys_nsw6<TF>::has_mask(std::string name)
{
    return false;
}

template<typename TF>
void Microphys_nsw6<TF>::get_mask(Stats<TF>& stats, std::string mask_name)
{
    std::string message = "NSW6 microphysics scheme can not provide mask: \"" + mask_name +"\"";
    throw std::runtime_error(message);
}

template class Microphys_nsw6<double>;
template class Microphys_nsw6<float>;
