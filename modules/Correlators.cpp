#include "Correlators.h"

//#define DILUTION_ITERATOR_PRINT

#include "DilutedFactor.h"
#include "QuarkLineBlock2.h"
#include "StopWatch.h"
#include "dilution-iterator.h"
#include "h5-wrapper.h"
#include "typedefs.h"
#include "Diagram.h"
#include "Reduction.h"

int get_time_delta(BlockIterator const &slice_pair, int const Lt) {
  return abs((slice_pair.sink() - slice_pair.source() - Lt) % Lt);
}

/******************************************************************************/
/******************************************************************************/

Correlators::Correlators(const size_t Lt,
                         const size_t dilT,
                         const size_t dilE,
                         const size_t nev,
                         const CorrelatorLookup &corr_lookup,
                         OperatorLookup const &operator_lookup,
                         QuarklineLookup const &quark_lookup)
    : Lt(Lt),
      dilT(dilT),
      dilE(dilE),
      nev(nev),
      dil_fac_lookup(
          {quark_lookup.Q0, quark_lookup.Q1, quark_lookup.Q2V, quark_lookup.Q2L}) {}

/*!
 *  @deprecated
 */
void Correlators::build_part_trQ1(RandomVector const &randomvectors,
                                  OperatorsForMesons const &meson_operator,
                                  Perambulator const &perambulators,
                                  std::vector<CorrInfo> const &corr_lookup,
                                  std::string const output_path,
                                  std::string const output_filename) {
  if (corr_lookup.empty())
    return;

  StopWatch swatch("C1");

  corr_part_trQ1.resize(boost::extents[corr_lookup.size()][Lt]);

  DilutionScheme const dilution_scheme(Lt, dilT, DilutionType::block);

#pragma omp parallel
  {
    swatch.start();

    QuarkLineBlock2<QuarkLineType::Q1> quarklines(
        randomvectors, perambulators, meson_operator, dilT, dilE, nev, dil_fac_lookup.Q1);

#pragma omp for schedule(dynamic)
    for (int t = 0; t < Lt; ++t) {
      auto const b = dilution_scheme.time_to_block(t);

      for (const auto &c_look : corr_lookup) {
        corr_part_trQ1[c_look.id][t] =
            factor_to_trace(quarklines[{t, b}].at({c_look.lookup[0]}));
      }
    }

    swatch.stop();
  }  // parallel part ends here

  HDF5Handle handle(output_path, "C1", output_filename);

  // normalisation
  for (const auto &c_look : corr_lookup) {
    for (auto &corr_t : corr_part_trQ1[c_look.id]) {
      for (auto &diluted_trace : corr_t) {
        // TODO: Hard Coded atm - Be carefull
        diluted_trace.data /= Lt;
      }
    }

    auto group = handle.create_group(c_look.hdf5_dataset_name);
    std::cout << "Going to write" << std::endl;
    write_heterogenious(group, corr_part_trQ1[c_look.id]);
  }
  swatch.print();
}

// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
void Correlators::build_corr0(RandomVector const &randomvectors,
                              OperatorsForMesons const &meson_operator,
                              Perambulator const &perambulators,
                              std::vector<CorrInfo> const &corr_lookup) {
  if (corr_lookup.empty())
    return;

  StopWatch swatch("corr0");

  corr0.resize(boost::extents[corr_lookup.size()][Lt][Lt]);

  DilutionScheme const dilution_scheme(Lt, dilT, DilutionType::block);

#pragma omp parallel
  {
    swatch.start();

    QuarkLineBlock2<QuarkLineType::Q1> quarklines(
        randomvectors, perambulators, meson_operator, dilT, dilE, nev, dil_fac_lookup.Q1);

#pragma omp for schedule(dynamic)
    for (int b = 0; b < dilution_scheme.size(); ++b) {
      // Notation is that `t1` is the source and `t2` the sink. Both will be
      // done eventually, so this is symmetric.

      auto const block_pair = dilution_scheme[b];

      for (auto const slice_pair : block_pair) {
        for (const auto &c_look : corr_lookup) {
          corr0[c_look.id][slice_pair.source()][slice_pair.sink()] = factor_to_trace(
              quarklines[{slice_pair.source(), slice_pair.sink_block()}].at(
                  {c_look.lookup[0]}),
              quarklines[{slice_pair.sink(), slice_pair.source_block()}].at(
                  {c_look.lookup[1]}));
        }
      }
    }
    swatch.stop();
  }

  swatch.print();
}

// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
void Correlators::build_corrC(RandomVector const &randomvectors,
                              Perambulator const &perambulators,
                              OperatorsForMesons const &meson_operator,
                              std::vector<CorrInfo> const &corr_lookup) {
  if (corr_lookup.size() == 0)
    return;

  StopWatch swatch("corrC");

  corrC.resize(boost::extents[corr_lookup.size()][Lt][Lt]);

  DilutionScheme const dilution_scheme(Lt, dilT, DilutionType::block);

#pragma omp parallel
  {
    swatch.start();

    QuarkLineBlock2<QuarkLineType::Q0> quarklines_Q0(
        randomvectors, perambulators, meson_operator, dilT, dilE, nev, dil_fac_lookup.Q0);
    QuarkLineBlock2<QuarkLineType::Q2> quarklines_Q2V(randomvectors,
                                                      perambulators,
                                                      meson_operator,
                                                      dilT,
                                                      dilE,
                                                      nev,
                                                      dil_fac_lookup.Q2V);

#pragma omp for schedule(dynamic)
    for (int b = 0; b < dilution_scheme.size(); ++b) {
      auto const block_pair = dilution_scheme[b];

      for (auto const slice_pair : block_pair) {
        for (const auto &c_look : corr_lookup) {
          corrC[c_look.id][slice_pair.source()][slice_pair.sink()] =
              factor_to_trace(quarklines_Q0[{slice_pair.sink()}].at({c_look.lookup[1]}),
                              quarklines_Q2V[{slice_pair.sink_block(),
                                              slice_pair.source(),
                                              slice_pair.sink_block()}]
                                  .at({c_look.lookup[0]}));
        }
      }
    }
    swatch.stop();
  }

  swatch.print();
}

/******************************************************************************/
/*!
 *  @param quarklines       Instance of Quarklines. Contains prebuilt
 *                          combinations of operators and perambulators
 *  @param meson_operator   Instance of OperatorsForMesons. Contains
 *                          operators (@f$ V^\dagger V $f$) with momenta
 *                          and with/without dilution.
 *  @param perambulators    Instance of Perambulator class. Contains
 *                          Perambulator data
 *  @param operator_lookup
 *  @param corr_lookup
 *  @param quark_lookup
 *
 *  If a diagram is not specified in the infile, corr_lookup contains an empty
 *  vector for this diagram and the build function immediately returns
 */
// void Correlators::contract (Quarklines& quarklines,
void Correlators::contract(OperatorsForMesons const &meson_operator,
                           RandomVector const &randomvectors,
                           Perambulator const &perambulators,
                           OperatorLookup const &operator_lookup,
                           CorrelatorLookup const &corr_lookup,
                           QuarklineLookup const &quark_lookup,
                           std::string const output_path,
                           std::string const output_filename) {
  build_part_trQ1(randomvectors,
                  meson_operator,
                  perambulators,
                  corr_lookup.C1,
                  output_path,
                  output_filename);

  // 1. Build all functions which need corrC and free it afterwards.
  build_corrC(randomvectors, perambulators, meson_operator, corr_lookup.corrC);

  // 2. Build all functions which need corr0 and free it afterwards.
  build_corr0(randomvectors, meson_operator, perambulators, corr_lookup.corr0);

  // 3. Build all other correlation functions.

  // XXX If we had C++14, we could do `make_unique`.
  std::vector<std::unique_ptr<Diagram>> diagrams;

  diagrams.emplace_back(new C2c(corr_lookup.C2c, output_path, output_filename, Lt));
  diagrams.emplace_back(new C20(corr_lookup.C20, output_path, output_filename, Lt));
  diagrams.emplace_back(new C20V(corr_lookup.C20V, output_path, output_filename, Lt));

  diagrams.emplace_back(new C3c(corr_lookup.C3c, output_path, output_filename, Lt));
  diagrams.emplace_back(new C30(corr_lookup.C30, output_path, output_filename, Lt));

  diagrams.emplace_back(new C4cB(corr_lookup.C4cB, output_path, output_filename, Lt));
  diagrams.emplace_back(new C40B(corr_lookup.C40B, output_path, output_filename, Lt));
  diagrams.emplace_back(new C4cC(corr_lookup.C4cC, output_path, output_filename, Lt));
  diagrams.emplace_back(new C40C(corr_lookup.C40C, output_path, output_filename, Lt));

  diagrams.emplace_back(new C4cD(corr_lookup.C4cD, output_path, output_filename, Lt));
  diagrams.emplace_back(new C40D(corr_lookup.C40D, output_path, output_filename, Lt));
  diagrams.emplace_back(new C4cV(corr_lookup.C4cV, output_path, output_filename, Lt));
  diagrams.emplace_back(new C40V(corr_lookup.C40V, output_path, output_filename, Lt));

  DilutionScheme const dilution_scheme(Lt, dilT, DilutionType::block);

#pragma omp parallel
  {
    QuarkLineBlockCollection part_collection(randomvectors,
                                             perambulators,
                                             meson_operator,
                                             dilT,
                                             dilE,
                                             nev,
                                             dil_fac_lookup,
                                             corr0,
                                             corrC,
                                             corr_part_trQ1);

#pragma omp for schedule(dynamic)
    for (int b = 0; b < dilution_scheme.size(); ++b) {
      auto const block_pair = dilution_scheme[b];

      for (auto &diagram : diagrams) {
        for (auto const slice_pair : block_pair) {
          int const t = get_time_delta(slice_pair, Lt);

          diagram->contract(t, slice_pair, part_collection);
        }
      }

      part_collection.clear();
    }

    for (auto &diagram : diagrams) {
      diagram->reduce();
    }
  }

  for (auto &diagram : diagrams) {
    diagram->write();
  }
}
