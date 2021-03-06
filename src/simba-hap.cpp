// ============================================================================
// Copyright 2016 IBM Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS"
// BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language
// governing permissions and limitations under the License.
// ============================================================================

#include <seqan/basic.h>
#include <seqan/arg_parse.h>
#include <seqan/vcf_io.h>

#include <boost/multi_array.hpp>

#include <lemon/lp.h>

// ============================================================================
// View
// ============================================================================

// ----------------------------------------------------------------------------
// Class reference_view
// ----------------------------------------------------------------------------

template <class T>
class reference_view
{
    T* value_;

public:
    reference_view() :
        value_()
    {}

    reference_view(T & value) :
        value_(std::addressof(value))
    {}

    T & get() const
    {
        SEQAN_ASSERT(value_);
        return *value_;
    }

    operator T & () const
    {
        SEQAN_ASSERT(value_);
        return *value_;
    }

    reference_view<T> operator=(T & value)
    {
        value_ = std::addressof(value);
        return *this;
    }
};

// ----------------------------------------------------------------------------
// Function view
// ----------------------------------------------------------------------------

template <class T>
inline reference_view<T> view(T & t)
{
    return reference_view<T>(t);
}

// ============================================================================
// Extensions to STL
// ============================================================================

// ----------------------------------------------------------------------------
// Operator <<
// ----------------------------------------------------------------------------

namespace std
{
template <typename T1, typename T2>
std::ostream& operator<<(std::ostream & s, std::pair<T1, T2> const & p)
{
    s << '<' << std::get<0>(p) << ',' << std::get<1>(p) << '>';
    return s;
}

template <typename T>
std::ostream& operator<<(std::ostream & s, std::vector<T> const & v)
{
    s << "[ ";
    std::copy(v.begin(), v.end(), std::ostream_iterator<T>(s, " "));
    s << "]";
    return s;
}

template <typename T, size_t SIZE>
std::ostream& operator<<(std::ostream & s, std::array<T, SIZE> const & v)
{
    s << "[ ";
    std::copy(v.begin(), v.end(), std::ostream_iterator<T>(s, " "));
    s << "]";
    return s;
}
}

// ============================================================================
// Extensions to Boost
// ============================================================================

// ----------------------------------------------------------------------------
// Operator <<
// ----------------------------------------------------------------------------

namespace boost
{
template <typename T, size_t SIZE>
std::ostream& operator<<(std::ostream & s, detail::multi_array::const_sub_array<T, SIZE> const & m)
{
    typedef typename detail::multi_array::const_sub_array<T, SIZE>::value_type value_t;

    s << "[ ";
    std::copy(m.data(), m.data() + m.num_elements(), std::ostream_iterator<value_t>(s, " "));
    s << "]";
    return s;
}

template <typename T, size_t SIZE>
std::ostream& operator<<(std::ostream & s, boost::multi_array<T, SIZE> const & m)
{
    typedef typename boost::multi_array<T, SIZE>::value_type value_t;
    
    s << "[ ";
    std::copy(m.begin(), m.end(), std::ostream_iterator<value_t>(s, " "));
    s << "]";
    return s;
}
}

// ============================================================================
// VCF I/O
// ============================================================================

// ----------------------------------------------------------------------------
// Function read_alleles()
// ----------------------------------------------------------------------------
// Read ref/alt strings.

//template <typename alleles_t>
//inline void read_alleles(alleles_t & alleles, seqan::VcfRecord const & record)
//{
//    front(alleles) = record.ref;
//    clear(back(alleles));
//    auto alleles_it = directionIterator(record.alt, seqan::Input());
//    seqan::readUntil(back(alleles), alleles_it, seqan::EqualsChar<','>());
//
//    clear(alleles);
//    appendValue(alleles, record.ref);
//    strSplit(alleles, record.alt, seqan::EqualsChar<','>());
//}

// ----------------------------------------------------------------------------
// Function read_genotype()
// ----------------------------------------------------------------------------
// Read genotype string.

template <typename genotype_t, typename genotype_info_t>
inline void read_genotype(genotype_t & genotype, genotype_info_t const & genotype_info)
{
    genotype.clear();

    // Read first field, assuming it is GT.
    auto genotype_it = directionIterator(genotype_info, seqan::Input());
    seqan::readUntil(genotype, genotype_it, seqan::EqualsChar<':'>());

    // Convert genotype string to vector by removing slashes.
    genotype.erase(std::remove(genotype.begin(), genotype.end(), '/'), genotype.end());
    genotype.erase(std::remove(genotype.begin(), genotype.end(), '|'), genotype.end());
}

// ----------------------------------------------------------------------------
// Function write_genotype()
// ----------------------------------------------------------------------------
// Write genotype string.

template <typename genotype_info_t, typename genotype_t>
inline void write_genotype(genotype_info_t & genotype_info, genotype_t const & genotype)
{
    seqan::clear(genotype_info);

    std::for_each(genotype.begin(), genotype.end() - 1, [&genotype_info](auto allele)
    {
        seqan::appendNumber(genotype_info, allele.get());
        seqan::appendValue(genotype_info, '|');
    });

    seqan::appendNumber(genotype_info, genotype.back().get());
}

// ============================================================================
// Genotypes
// ============================================================================

// ----------------------------------------------------------------------------
// Function get_ploidy()
// ----------------------------------------------------------------------------

template <typename genotype_t>
inline uint32_t get_ploidy(genotype_t const & genotype)
{
    return genotype.size();
}

// ----------------------------------------------------------------------------
// Function is_unknown()
// ----------------------------------------------------------------------------

template <typename genotype_t>
inline bool is_unknown(genotype_t const & genotype)
{
    return genotype.back() == '.';
}

// ----------------------------------------------------------------------------
// Function get_dosage()
// ----------------------------------------------------------------------------

template <typename genotype_t, typename value_t>
inline uint32_t get_dosage(genotype_t const & genotype, value_t value)
{
    return std::count(genotype.begin(), genotype.end(), value);
}

// ============================================================================
// Dosages
// ============================================================================

// ----------------------------------------------------------------------------
// Type dosages_distribution
// ----------------------------------------------------------------------------
// dosages_distribution = [f(0),f(1),...,f(p)]

template <typename value_t, uint32_t n_ploidy>
using dosages_distribution = std::array<value_t, n_ploidy + 1u>;

// ----------------------------------------------------------------------------
// Function make_dosages_distribution()
// ----------------------------------------------------------------------------

template <uint32_t n_ploidy, typename genotypes_t>
inline dosages_distribution<uint32_t, n_ploidy> make_dosages_distribution(genotypes_t const & genotypes)
{
    dosages_distribution<uint32_t, n_ploidy> dosages_d;

    std::fill(dosages_d.begin(), dosages_d.end(), 0u);

    for (auto const & genotype : genotypes)
        dosages_d[get_dosage(genotype, 1u)]++;

    return dosages_d;
}

// ----------------------------------------------------------------------------
// Function normalize_dosages_distribution()
// ----------------------------------------------------------------------------

template <typename dosages_distribution_t>
inline void normalize_dosages_distribution(dosages_distribution_t & dosages_d, uint32_t n_samples)
{
    auto dosages_sum = std::accumulate(dosages_d.begin(), dosages_d.end(), 0.0f);
    std::transform(dosages_d.begin(), dosages_d.end(), dosages_d.begin(),
                  [n_samples, dosages_sum](auto d) { return n_samples * d / dosages_sum; });
}

// ----------------------------------------------------------------------------
// Function l1_norm()
// ----------------------------------------------------------------------------

template <typename dosages_distribution_a_t, typename dosages_distribution_b_t>
inline typename dosages_distribution_a_t::value_type
l1_norm(dosages_distribution_a_t const & a, dosages_distribution_b_t const & b)
{
    typedef typename dosages_distribution_a_t::value_type value_t;

    return std::inner_product(a.begin(), a.end(), b.begin(), value_t(), std::plus<value_t>(),
                              [](auto a_i, auto b_i){ return std::abs(a_i - b_i); });
}

// ----------------------------------------------------------------------------
// Type dosages_vector
// ----------------------------------------------------------------------------

template <typename value_t, uint32_t n_ploidy>
using dosages_vector = std::vector<dosages_distribution<value_t, n_ploidy>>;

// ----------------------------------------------------------------------------
// Function normalize_dosages_vector()
// ----------------------------------------------------------------------------

template <typename dosages_vector_t>
inline void normalize_dosages_vector(dosages_vector_t & dosages_v, uint32_t n_samples)
{
    std::for_each(dosages_v.begin(), dosages_v.end(), [n_samples](auto & dosages_d)
    {
        normalize_dosages_distribution(dosages_d, n_samples);
    });
}

// ============================================================================
// Variants
// ============================================================================

// ----------------------------------------------------------------------------
// Class variants
// ----------------------------------------------------------------------------
// Chromosomal position and ref/alt strings.

class variants
{
public:
    typedef std::pair<uint32_t, uint32_t> position_t;
    typedef std::array<seqan::IupacString, 2> alleles_t;

    std::vector<position_t> positions; // positions[m] = (chr, pos)
    std::vector<alleles_t> alleles;    // alleles[m] = (ref, alt)
};

// ----------------------------------------------------------------------------
// Function simulate_variant_positions()
// ----------------------------------------------------------------------------

template <typename positions_vector_t, typename generator_t>
inline void simulate_variant_positions(positions_vector_t & positions_v, generator_t && generator)
{
    seqan::ignoreUnusedVariableWarning(positions_v);
    seqan::ignoreUnusedVariableWarning(generator);
}

// ----------------------------------------------------------------------------
// Function simulate_variant_alleles()
// ----------------------------------------------------------------------------

// ============================================================================
// Contig names
// ============================================================================

typedef typename seqan::FormattedFileContext<seqan::VcfFileIn, void>::TNameStore contig_names_store;

// ============================================================================
// Founders
// ============================================================================

typedef std::vector<uint32_t> founders_distribution;

// ----------------------------------------------------------------------------
// Function simulate_founders_distribution()
// ----------------------------------------------------------------------------

template <typename founders_distribution_t, typename generator_t>
inline void simulate_founders_distribution(founders_distribution_t & founders_d,
                                           uint32_t n_founders,
                                           uint32_t n_samples,
                                           uint8_t n_ploidy,
                                           generator_t && generator)
{
    SEQAN_ASSERT_GT(n_samples, 0u);
    SEQAN_ASSERT_LEQ(n_founders, n_samples * n_ploidy);

    founders_d.resize(n_founders, 1u);

    std::uniform_int_distribution<uint32_t> distribution(0u, n_founders - 1);
//    std::normal_distribution<uint32_t> distribution(0u, n_founders - 1);

    for (uint32_t i = 0; i < n_samples * n_ploidy - n_founders; i++)
        founders_d[distribution(generator)]++;

    std::cerr << "FOUNDERS DISTRIBUTION: " << founders_d << std::endl;
    std::cerr << "=================================================================" << std::endl << std::endl;
}

// ============================================================================
// Haplotypes map
// ============================================================================
// Map: Sample X Haplotype -> Founder.

typedef boost::multi_array<uint32_t, 2> haplotypes_map; // [sample][haplotype]

// ----------------------------------------------------------------------------
// Function simulate_haplotypes_map()
// ----------------------------------------------------------------------------

template <typename haplotypes_map_t, typename founders_distribution_t, typename generator_t>
inline void simulate_haplotypes_map(haplotypes_map_t & haplotypes_m,
                                    founders_distribution_t const & founders_d,
                                    uint32_t n_samples,
                                    uint8_t n_ploidy,
                                    generator_t && generator)
{
    SEQAN_ASSERT_EQ(std::accumulate(founders_d.begin(), founders_d.end(), 0u), n_samples * n_ploidy);
//    SEQAN_ASSERT_EQ(haplotypes_m.num_elements(), n_samples * n_ploidy);

    haplotypes_m.resize(boost::extents[n_samples][n_ploidy]);

    auto haplotypes_m_begin = haplotypes_m.data();
    auto haplotypes_m_it = haplotypes_m.data();
    auto haplotypes_m_end = haplotypes_m.data() + haplotypes_m.num_elements();
    for (auto founders_d_it = founders_d.begin(); founders_d_it != founders_d.end(); ++founders_d_it)
        haplotypes_m_it = std::fill_n(haplotypes_m_it, *founders_d_it, founders_d_it - founders_d.begin());
    SEQAN_ASSERT(haplotypes_m_it == haplotypes_m_end);

    std::shuffle(haplotypes_m_begin, haplotypes_m_end, generator);

    std::cerr << "HAPLOTYPES MAP: " << haplotypes_m << std::endl;
    std::cerr << "=================================================================" << std::endl << std::endl;
}

// ============================================================================
// Haplotypes
// ============================================================================

// ----------------------------------------------------------------------------
// Type founders_alts_vector
// ----------------------------------------------------------------------------

typedef std::vector<uint16_t> founders_alts; // [founder]
typedef std::vector<founders_alts> founders_alts_vector; // [marker][founder]

// ----------------------------------------------------------------------------
// Type samples_alts_vector
// ----------------------------------------------------------------------------
// View: Allele[Sample, Haplotype] -> Allele[Founder].

template <uint32_t n_ploidy>
using haplotypes_alts = std::array<reference_view<uint16_t>, n_ploidy>; // [haplotype]

template <uint32_t n_ploidy>
using samples_alts = std::vector<haplotypes_alts<n_ploidy>>; // [sample][haplotype]

template <uint32_t n_ploidy>
using samples_alts_vector = std::vector<samples_alts<n_ploidy>>; // [marker][sample][haplotype]

// ----------------------------------------------------------------------------
// Function fill_samples_alts()
// ----------------------------------------------------------------------------

template <typename samples_alts_t, typename founders_alts_t, typename haplotypes_map_t>
inline void fill_samples_alts(samples_alts_t & samples_alts,
                              founders_alts_t & founders_alts,
                              haplotypes_map_t const & haplotypes_m)
{
    uint32_t n_samples = haplotypes_m.size();
    uint32_t n_ploidy = haplotypes_m.shape()[1];

    samples_alts.resize(n_samples);

    for (uint32_t sample_id = 0; sample_id < n_samples; ++sample_id)
        for (uint8_t haplotype_id = 0; haplotype_id < n_ploidy; ++haplotype_id)
            samples_alts[sample_id][haplotype_id] = founders_alts[haplotypes_m[sample_id][haplotype_id]];
}

// ----------------------------------------------------------------------------
// Function fill_samples_alts_vector()
// ----------------------------------------------------------------------------

template <typename samples_alts_vector_t, typename founders_alts_vector_t, typename haplotypes_map_t>
inline void fill_samples_alts_vector(samples_alts_vector_t & samples_alts_v,
                                     founders_alts_vector_t & founders_alts_v,
                                     haplotypes_map_t const & haplotypes_m)
{
    uint32_t n_markers = founders_alts_v.size();

    samples_alts_v.resize(n_markers);

    for (uint32_t marker_id = 0; marker_id < n_markers; ++marker_id)
        fill_samples_alts(samples_alts_v[marker_id], founders_alts_v[marker_id], haplotypes_m);
}

// ----------------------------------------------------------------------------
// Function cerr_samples_alts_vector()
// ----------------------------------------------------------------------------

template <typename samples_alts_vector_t>
inline void cerr_samples_alts_vector(samples_alts_vector_t const & samples_alts_v)
{
    std::for_each(samples_alts_v.begin(), samples_alts_v.end(), [](auto & samples_alts_m)
    {
        std::cerr << "SAMPLES ALLELE: " << samples_alts_m << std::endl;
    });
    std::cerr << "=================================================================" << std::endl << std::endl;
}

// ============================================================================
// Fitting
// ============================================================================

// ----------------------------------------------------------------------------
// Class random_fitting
// ----------------------------------------------------------------------------

//template <typename generator_t>
//class random_fitting
//{
//public:
//    template <typename founders_alts_t, typename dosages_distribution_t>
//    inline float fit(founders_alts_t & founders_alts, dosages_distribution_t const & dosages_d);
//
//    random_fitting(generator_t & generator) :
//        generator(generator)
//    {}
//
//private:
//    generator_t & generator;
//    std::bernoulli_distribution distribution;
//};

// ----------------------------------------------------------------------------
// Method random_fitting::fit()
// ----------------------------------------------------------------------------

//template <typename generator_t> template <typename founders_alts_t, typename dosages_distribution_t>
//inline float random_fitting<generator_t>::fit(founders_alts_t & founders_alts, dosages_distribution_t const & /* dosages_d_in */)
//{
//    std::generate(founders_alts.begin(), founders_alts.end(), [this]()
//    {
//        return distribution(generator);
//    });
//
//    return 0.0;
//}

// ----------------------------------------------------------------------------
// Class descent_fitting
// ----------------------------------------------------------------------------

template <uint8_t n_ploidy>
class descent_fitting
{
public:
    descent_fitting(haplotypes_map const & haplotypes_m) :
        haplotypes_m(haplotypes_m)
    {}

    template <typename founders_alts_t, typename dosages_distribution_t>
    inline float fit(founders_alts_t & founders_alts, dosages_distribution_t const & dosages_d);

private:
    haplotypes_map const & haplotypes_m;

    samples_alts<n_ploidy> samples_alts_view;
    std::vector<float> distances;
};

// ----------------------------------------------------------------------------
// Method descent_fitting::fit()
// ----------------------------------------------------------------------------

template <uint8_t n_ploidy> template <typename founders_alts_t, typename dosages_distribution_t>
inline float descent_fitting<n_ploidy>::fit(founders_alts_t & founders_alts, dosages_distribution_t const & dosages_d_in)
{
    fill_samples_alts(samples_alts_view, founders_alts, haplotypes_m);

    std::fill(founders_alts.begin(), founders_alts.end(), 0u);

    uint32_t n_founders = founders_alts.size();
    distances.resize(n_founders);

    auto dosages_d_zero = make_dosages_distribution<n_ploidy>(samples_alts_view);
    auto distance = l1_norm(dosages_d_in, dosages_d_zero);

    for (uint32_t ones = 0; ones < n_founders; ones++)
    {
//        auto dosages_d_x = make_dosages_distribution<n_ploidy>(samples_alts_view);
//        std::cerr << "CURRENT DISTANCE: " << distance << " = " << std::make_pair(dosages_d_in, dosages_d_x) << std::endl;

        std::transform(founders_alts.begin(), founders_alts.end(), distances.begin(),
                      [this, &founders_alts, &dosages_d_in](auto & alt)
        {
            if (alt == 1) return std::numeric_limits<float>::max();

            alt = 1;
            auto dosages_d_x = make_dosages_distribution<n_ploidy>(samples_alts_view);
            alt = 0;
//            std::cerr << "DISTANCE X: " << l1_norm(dosages_d_in, dosages_d_x) << " = " << std::make_pair(dosages_d_in, dosages_d_x) << std::endl;
            return l1_norm(dosages_d_in, dosages_d_x);
        });

        auto distances_it = std::min_element(distances.begin(), distances.end());
        auto min_distance = *distances_it;
        auto min_i = distances_it - distances.begin();
//        std::cerr << "NEXT DISTANCES: " << distances << std::endl;

        if (distance < min_distance)
            break;

        distance = min_distance;
        founders_alts[min_i] = 1;
    }

    auto dosages_d_out = make_dosages_distribution<n_ploidy>(samples_alts_view);
    std::cerr << "DISTANCE: " << distance << " = " << std::make_pair(dosages_d_in, dosages_d_out) << std::endl;
//    std::cerr << "- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -" << std::endl << std::endl;

    return distance;
}

// ----------------------------------------------------------------------------
// Class mip_fitting
// ----------------------------------------------------------------------------

#ifdef LEMON_HAVE_MIP
template <uint8_t n_ploidy>
class mip_fitting
{
public:
    mip_fitting(haplotypes_map const & haplotypes_m, uint32_t n_founders) :
        haplotypes_m(haplotypes_m)
    {
        init(n_founders);
    }

    template <typename founders_alts_t, typename dosages_distribution_t>
    inline float fit(founders_alts_t & founders_alts, dosages_distribution_t const & dosages_d);

private:
    typedef dosages_distribution<lemon::Mip::Col, n_ploidy> mip_cols_t;
    typedef dosages_distribution<lemon::Mip::Row, n_ploidy> mip_rows_t;

    lemon::Mip mip;
    mip_cols_t mip_z;                       // [dosage]
    mip_cols_t mip_dosages;                 // [dosage]
    mip_rows_t mip_dosages_plus;            // [dosage]
    mip_rows_t mip_dosages_minus;           // [dosage]
    std::vector<mip_cols_t> mip_errors;     // [sample][dosage]
    std::vector<mip_cols_t> mip_indicators; // [sample][dosage]
    std::vector<lemon::Mip::Col> mip_alts;  // [founder]

    haplotypes_map const & haplotypes_m;

    inline void init(uint32_t n_founders);
};

// ----------------------------------------------------------------------------
// Method mip_fitting::init()
// ----------------------------------------------------------------------------

template <uint8_t n_ploidy>
inline void mip_fitting<n_ploidy>::init(uint32_t n_founders)
{
    auto n_samples = haplotypes_m.size();

    // Vector z to linearize l1-norm objective.
    SEQAN_ASSERT_EQ(mip_dosages.size(), n_ploidy + 1u);
    std::generate(mip_z.begin(), mip_z.end(), [this]() { return mip.addCol(); });
    std::for_each(mip_z.begin(), mip_z.end(), [this](auto z)
    {
        mip.colType(z, lemon::Mip::REAL);
        mip.colLowerBound(z, 0);
    });

    // Dosages d.
    SEQAN_ASSERT_EQ(mip_dosages.size(), n_ploidy + 1u);
    std::generate(mip_dosages.begin(), mip_dosages.end(), [this]() { return mip.addCol(); });
    std::for_each(mip_dosages.begin(), mip_dosages.end(), [this](auto d)
    {
        mip.colType(d, lemon::Mip::INTEGER);
        mip.colLowerBound(d, 0);
    });

    // Dosage absolute errors e_s,p.
    mip_errors.resize(n_samples);
    std::for_each(mip_errors.begin(), mip_errors.end(), [this](auto & mip_sample)
    {
        std::generate(mip_sample.begin(), mip_sample.end(), [this]() { return mip.addCol(); });
        std::for_each(mip_sample.begin(), mip_sample.end(), [this](auto e)
        {
            mip.colType(e, lemon::Mip::REAL);
            mip.colLowerBound(e, 0);
        });
    });

    // Dosage indicators i_s,p.
    mip_indicators.resize(n_samples);
    std::for_each(mip_indicators.begin(), mip_indicators.end(), [this](auto & mip_sample)
    {
        std::generate(mip_sample.begin(), mip_sample.end(), [this]() { return mip.addCol(); });
        std::for_each(mip_sample.begin(), mip_sample.end(), [this](auto i)
        {
            mip.colType(i, lemon::Mip::INTEGER);
            mip.colLowerBound(i, 0);
//            mip.colUpperBound(i, 1);
        });
    });

    // Founder alleles f.
    mip_alts.resize(n_founders);
    std::generate(mip_alts.begin(), mip_alts.end(), [this]() { return mip.addCol(); });
    std::for_each(mip_alts.begin(), mip_alts.end(), [this](auto a)
    {
        mip.colType(a, lemon::Mip::INTEGER);
        mip.colLowerBound(a, 0);
        mip.colUpperBound(a, 1);
    });

    // Constraint d_p = Σ_s i_s,p.
    for (uint8_t dosage = 0; dosage < n_ploidy + 1; dosage++)
    {
        lemon::Mip::Expr i_sum;
        for (uint32_t sample_id = 0; sample_id < n_samples; sample_id++)
            i_sum += mip_indicators[sample_id][dosage];
        mip.addRow(mip_dosages[dosage] == i_sum);
    }

    // Constraint Σ_p i_s,p = 1.
    for (uint32_t sample_id = 0; sample_id < n_samples; sample_id++)
    {
        lemon::Mip::Expr i_sum;
        for (uint8_t dosage = 0; dosage < n_ploidy + 1; dosage++)
            i_sum += mip_indicators[sample_id][dosage];
        mip.addRow(i_sum == 1u);
    }

    // Constraint e_s,p >= Σ h(f) - p.
    // Constraint e_s,p >= p - Σ h(f).
    // Constraint i_s,p <= k * (1 - e_s,p).
    for (uint32_t sample_id = 0; sample_id < n_samples; sample_id++)
    {
        lemon::Mip::Expr sample_sum;
        for (uint8_t haplotype_id = 0; haplotype_id < n_ploidy; haplotype_id++)
            sample_sum += mip_alts[haplotypes_m[sample_id][haplotype_id]];

        for (uint8_t dosage = 0; dosage < n_ploidy + 1; dosage++)
        {
            mip.addRow(mip_errors[sample_id][dosage] >= dosage - sample_sum);
            mip.addRow(mip_errors[sample_id][dosage] >= sample_sum - dosage);

            mip.addRow(mip_errors[sample_id][dosage] <= n_ploidy * (1u - mip_indicators[sample_id][dosage]));
        }
    }

    // Minimize Σ z.
    lemon::Mip::Expr o = std::accumulate(mip_z.begin(), mip_z.end(), lemon::Mip::Expr());
    mip.obj(o);
    mip.min();
}

// ----------------------------------------------------------------------------
// Method mip_fitting::fit()
// ----------------------------------------------------------------------------

template <uint8_t n_ploidy> template <typename founders_alts_t, typename dosages_distribution_t>
inline float mip_fitting<n_ploidy>::fit(founders_alts_t & founders_alts, dosages_distribution_t const & dosages_d_in)
{
    // Constraint d - d_obs <= z.
    // Constraint d_obs - d <= z.
    for (uint8_t dosage = 0; dosage < n_ploidy + 1; dosage++)
    {
        mip_dosages_plus[dosage] = mip.addRow(mip_dosages[dosage] - dosages_d_in[dosage] <= mip_z[dosage]);
        mip_dosages_minus[dosage] = mip.addRow(dosages_d_in[dosage] - mip_dosages[dosage] <= mip_z[dosage]);
    }

//    mip.messageLevel(lemon::LpBase::MESSAGE_VERBOSE);
    mip.solve();
    SEQAN_ASSERT_EQ(mip.type(), lemon::MipSolver::OPTIMAL);
    float distance = mip.solValue();

    dosages_distribution<uint32_t, n_ploidy> dosages_d_out;
    std::transform(mip_dosages.begin(), mip_dosages.end(), dosages_d_out.begin(), [this](auto d){ return std::round(mip.sol(d)); });
    std::cerr << "DISTANCE: " << distance << " = " << std::make_pair(dosages_d_in, dosages_d_out) << std::endl;

//    std::cerr << "- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -" << std::endl << std::endl;
//
//    std::for_each(mip_errors.begin(), mip_errors.end(), [this](auto const & sample_errors)
//    {
//        dosages_distribution<double, n_ploidy> errors;
//        std::transform(sample_errors.begin(), sample_errors.end(), errors.begin(), [this](auto e){ return mip.sol(e); });
//        std::cerr << "ERRORS: " << errors << std::endl;
//    });
//
//    std::cerr << "- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -" << std::endl << std::endl;
//
//    std::for_each(mip_indicators.begin(), mip_indicators.end(), [this](auto const & sample_indicators)
//    {
//        dosages_distribution<bool, n_ploidy> indicators;
//        std::transform(sample_indicators.begin(), sample_indicators.end(), indicators.begin(), [this](auto i){ return std::round(mip.sol(i)); });
//        std::cerr << "INDICATORS: " << indicators << std::endl;
//    });
//
//    std::cerr << "- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -" << std::endl << std::endl;
//
//    dosages_distribution<double, n_ploidy> zeds;
//    std::transform(mip_z.begin(), mip_z.end(), zeds.begin(), [this](auto z){ return mip.sol(z); });
//    std::cerr << "ZEDS: " << zeds << std::endl;
//
//    std::cerr << "-----------------------------------------------------------------" << std::endl << std::endl;

    SEQAN_ASSERT_EQ(std::accumulate(dosages_d_out.begin(), dosages_d_out.end(), 0u), haplotypes_m.size());

    // Update founder alleles.
    std::transform(mip_alts.begin(), mip_alts.end(), founders_alts.begin(), [this](auto a){ return mip.sol(a); });

    // Remove dosage constraints.
    std::for_each(mip_dosages_plus.begin(), mip_dosages_plus.end(), [this](auto row) { mip.erase(row); });
    std::for_each(mip_dosages_minus.begin(), mip_dosages_minus.end(), [this](auto row) { mip.erase(row); });

    return distance;
}
#endif // LEMON_HAVE_MIP

// ----------------------------------------------------------------------------
// Function fit_founders_alts_vector()
// ----------------------------------------------------------------------------

template <typename fitting_t, typename founders_alts_vector_t, typename dosages_vector_t>
inline void fit_founders_alts_vector(fitting_t & fitting, founders_alts_vector_t & founders_alts_v, dosages_vector_t const & dosages_v)
{
    uint32_t n_markers = founders_alts_v.size();

    double start = seqan::sysTime();
    float distances = 0u;
    for (uint32_t marker_id = 0; marker_id < n_markers; ++marker_id)
        distances += fitting.fit(founders_alts_v[marker_id], dosages_v[marker_id]);
    double seconds = seqan::sysTime() - start;

    std::cerr << "DISTANCES: " << distances << std::endl;
    std::cerr << "SECONDS: " << seconds << std::endl;

    std::cerr << "=================================================================" << std::endl << std::endl;

    std::for_each(founders_alts_v.begin(), founders_alts_v.end(), [](auto & founders_alt)
    {
        std::cerr << "FOUNDERS ALLELE: " << founders_alt << std::endl;
    });
    std::cerr << "=================================================================" << std::endl << std::endl;
}

// ============================================================================
// VCF I/O
// ============================================================================

// ----------------------------------------------------------------------------
// Function read_vcf()
// ----------------------------------------------------------------------------

template <uint8_t n_ploidy, typename contig_names_store_t, typename variants_t, typename dosages_vector_t>
inline void read_vcf(contig_names_store_t & contig_names,
                     variants_t & variants,
                     dosages_vector_t & dosages_v,
                     std::string const & vcf_filename_in)
{
    typedef typename variants_t::position_t position_t;
    typedef typename variants_t::alleles_t alleles_t;
    typedef typename dosages_vector_t::value_type dosages_distribution_t;

    typedef std::vector<char> genotype_t;

    position_t position;
    alleles_t alleles;
    dosages_distribution_t dosages_d;
    genotype_t genotype;

    seqan::VcfHeader vcf_header;
    seqan::VcfRecord vcf_record;

    seqan::VcfFileIn vcf_file_in(vcf_filename_in.c_str());
    readHeader(vcf_header, vcf_file_in);

    while (!atEnd(vcf_file_in))
    {
        readRecord(vcf_record, vcf_file_in);

        position_t position = std::make_pair(vcf_record.rID, vcf_record.beginPos);

        front(alleles) = vcf_record.ref;
        clear(back(alleles));
        auto alleles_it = directionIterator(vcf_record.alt, seqan::Input());
        seqan::readUntil(back(alleles), alleles_it, seqan::EqualsChar<','>());

        if (!seqan::atEnd(alleles_it))
        {
            std::cerr << "INPUT VARIANT @ " << position << " POLYALLELIC" << std::endl;
            continue;
        }

        SEQAN_ASSERT_EQ(dosages_d.size(), n_ploidy + 1u);
        std::fill(dosages_d.begin(), dosages_d.end(), 0u);

        // Read all sample genotypes.
        for (auto const & genotype_info : vcf_record.genotypeInfos)
        {
            read_genotype(genotype, genotype_info);

            // Skip unknown genotypes.
            if (is_unknown(genotype))
            {
                std::cerr << "INPUT GENOTYPE @ " << position << " UNKNOWN" << std::endl;
                continue;
            }

            // Stop on wrong ploidy.
            if (get_ploidy(genotype) != n_ploidy)
                throw seqan::IOError("Input ploidy does not match VCF genotypes");

            dosages_d[get_dosage(genotype, '1')]++;
        }

        variants.positions.push_back(position);
        variants.alleles.push_back(alleles);
        dosages_v.push_back(dosages_d);

        std::cerr << "INPUT DOSAGES @ " << position << " # " << dosages_d << std::endl;
    }

    contig_names = contigNames(context(vcf_file_in));

    std::cerr << "=================================================================" << std::endl << std::endl;
}

// ----------------------------------------------------------------------------
// Function write_vcf()
// ----------------------------------------------------------------------------

template <typename contig_names_store_t, typename variants_t, typename samples_alts_vector_t>
inline void write_vcf(contig_names_store_t const & contig_names,
                      variants_t const & variants,
                      samples_alts_vector_t const & samples_alts_v,
                      std::string const & vcf_filename_out)
{
    uint32_t n_markers = samples_alts_v.size();
    uint32_t n_samples = samples_alts_v.front().size();

    // Open output file.
    seqan::VcfFileOut vcf_file_out;
    if (vcf_filename_out.empty())
        seqan::open(vcf_file_out, std::cout, seqan::Vcf());
    else
        seqan::_open(vcf_file_out, vcf_filename_out.c_str(), seqan::DefaultOpenMode<seqan::VcfFileOut>::VALUE, seqan::True());

    // Fill contig names.
    contigNames(context(vcf_file_out)) = contig_names;

    // Fill sample names.
    seqan::resize(sampleNames(context(vcf_file_out)), n_samples);
    for (uint32_t sample = 0; sample < n_samples; ++sample)
    {
        sampleNames(context(vcf_file_out))[sample] = "SAMPLE_";
        seqan::appendNumber(sampleNames(context(vcf_file_out))[sample], sample);
    }

    // Write VCF header.
    seqan::VcfHeader header_out;
    appendValue(header_out, seqan::VcfHeaderRecord("fileformat", "VCFv4.2"));
    appendValue(header_out, seqan::VcfHeaderRecord("FORMAT", "<ID=GT,Number=1,Type=String,Description=\"Genotype\">"));
    seqan::forEach(contigNames(context(vcf_file_out)), [&header_out](auto const & contigName)
    {
        appendValue(header_out, seqan::VcfHeaderRecord("contig",
                    std::string("<ID=") + seqan::toCString(contigName) + std::string(">")));
    });
    writeHeader(vcf_file_out, header_out);

    // Fill VCF record prototype.
    seqan::VcfRecord record_out;
    record_out.filter = "";
    record_out.info = "";
    record_out.format = "GT";
    seqan::resize(record_out.genotypeInfos, n_samples);

    // Write VCF records.
    for (uint32_t marker_id = 0; marker_id < n_markers; ++marker_id)
    {
        record_out.rID = std::get<0>(variants.positions[marker_id]);
        record_out.beginPos = std::get<1>(variants.positions[marker_id]);
        clear(record_out.id);
        appendNumber(record_out.id, marker_id);
        record_out.ref = front(variants.alleles[marker_id]);
        record_out.alt = back(variants.alleles[marker_id]);

        // Write VCF sample columns.
        for (uint32_t sample_id = 0; sample_id < n_samples; ++sample_id)
            write_genotype(record_out.genotypeInfos[sample_id], samples_alts_v[marker_id][sample_id]);

        writeRecord(vcf_file_out, record_out);
    }
}

// ============================================================================
// App
// ============================================================================

// ----------------------------------------------------------------------------
// Class app_options
// ----------------------------------------------------------------------------

struct app_options
{
    std::string vcf_filename_in;
    std::string vcf_filename_out;

    uint32_t n_ploidy;
    uint32_t n_founders;
    uint32_t n_samples;
    uint32_t n_markers;

    uint32_t seed;
    bool mip;

    app_options():
        n_ploidy(4),
        n_founders(1),
        n_samples(1),
        n_markers(1),
        seed(0),
        mip(false)
    {}

    void setup(seqan::ArgumentParser & parser) const
    {
        setAppName(parser, "SimBA-hap");
        setShortDescription(parser, "Haplotype simulator");
        setCategory(parser, "Simulation");

        setVersion(parser, SEQAN_APP_VERSION " [" SEQAN_REVISION "]");
        setDate(parser, SEQAN_DATE);

        addUsageLine(parser, "[\\fIOPTIONS\\fP]");

        addOption(parser, seqan::ArgParseOption("i", "input-vcf", "Input VCF file.", seqan::ArgParseOption::INPUT_FILE));
        setValidValues(parser, "input-vcf", seqan::VcfFileIn::getFileExtensions());
        setRequired(parser, "input-vcf");

        addOption(parser, seqan::ArgParseOption("o", "output-vcf", "Output VCF file.", seqan::ArgParseOption::OUTPUT_FILE));
        setValidValues(parser, "output-vcf", seqan::VcfFileOut::getFileExtensions());

        addOption(parser, seqan::ArgParseOption("p", "ploidy", "Organism ploidy.",
                                                seqan::ArgParseOption::INTEGER));
        setMinValue(parser, "ploidy", "2");
        setMaxValue(parser, "ploidy", "8");
        setDefaultValue(parser, "ploidy", n_ploidy);

        addOption(parser, seqan::ArgParseOption("f", "founders", "Number of founders to simulate.",
                                                seqan::ArgParseOption::INTEGER));
        setMinValue(parser, "founders", "1");
        setDefaultValue(parser, "founders", n_founders);

        addOption(parser, seqan::ArgParseOption("s", "samples", "Number of samples to simulate. Default: all samples in the input VCF file.",
                                                seqan::ArgParseOption::INTEGER));
        setMinValue(parser, "samples", "1");
        setDefaultValue(parser, "samples", n_samples);

        addOption(parser, seqan::ArgParseOption("m", "markers", "Number of markers to use. Default: all markers in the input VCF file.",
                                                seqan::ArgParseOption::INTEGER));
        setMinValue(parser, "markers", "1");
        setDefaultValue(parser, "markers", n_markers);

        addOption(parser, seqan::ArgParseOption("g", "seed", "Initial seed for pseudo-random number generation.",
                                                seqan::ArgParseOption::INTEGER));
        setMinValue(parser, "seed", "0");
        setDefaultValue(parser, "seed", seed);

#ifdef LEMON_HAVE_MIP
        addOption(parser, seqan::ArgParseOption("", "mip", "Compute optimal best-fit via Mixed-Integer Programming. \
                                                            Default: compute approximate fit via gradient descent."));
#endif
    }

    void parse(seqan::ArgumentParser const & parser)
    {
        getOptionValue(vcf_filename_in, parser, "input-vcf");
        getOptionValue(vcf_filename_out, parser, "output-vcf");
        getOptionValue(n_ploidy, parser, "ploidy");
        getOptionValue(n_founders, parser, "founders");
        getOptionValue(n_samples, parser, "samples");
        getOptionValue(n_markers, parser, "markers");
        getOptionValue(seed, parser, "seed");
#ifdef LEMON_HAVE_MIP
        getOptionValue(mip, parser, "mip");
#endif
    }
};

// ----------------------------------------------------------------------------
// Function run()
// ----------------------------------------------------------------------------

template <uint8_t n_ploidy>
void run(app_options const & options)
{
    std::mt19937 generator(options.seed);

    contig_names_store contig_names;
    variants variants;
    dosages_vector<float, n_ploidy> dosages_v;

    founders_distribution founders_d;
    haplotypes_map haplotypes_m;

    founders_alts_vector founders_alts_v;
    samples_alts_vector<n_ploidy> samples_alts_v;

    // Simulate or read markers.
    uint32_t n_markers = options.n_markers;
//    if (options.vcf_filename_in.empty())
//    {
//        simulate_variants_positions(variants, generator);
//        simulate_variants_alleles(variants, generator);
//        simulate_dosages(dosages_v, generator);
//    }
//    else
//    {
        read_vcf<n_ploidy>(contig_names, variants, dosages_v, options.vcf_filename_in);
        n_markers = dosages_v.size();
//    }

    // Normalize markers dosages by output samples.
    normalize_dosages_vector(dosages_v, options.n_samples);

    // Simulate founders distribution.
    simulate_founders_distribution(founders_d, options.n_founders, options.n_samples, n_ploidy, generator);

    // Simulate haplotypes map.
    simulate_haplotypes_map(haplotypes_m, founders_d, options.n_samples, n_ploidy, generator);

    // Resize haplotypes.
    founders_alts_v.resize(n_markers, founders_alts(options.n_founders));

    // Generate samples view.
    fill_samples_alts_vector(samples_alts_v, founders_alts_v, haplotypes_m);

    // Fit haplotypes to input dosages.
#ifdef LEMON_HAVE_MIP
    if (options.mip)
    {
        mip_fitting<n_ploidy> fitting(haplotypes_m, options.n_founders);
        fit_founders_alts_vector(fitting, founders_alts_v, dosages_v);
    }
    else
#endif
    {
        descent_fitting<n_ploidy> fitting(haplotypes_m);
        fit_founders_alts_vector(fitting, founders_alts_v, dosages_v);
    }
//    else
//    {
//        random_fitting<std::mt19937> fitting(generator);
//        fit_founders_alts_vector(fitting, founders_alts_v, dosages_v);
//    }

    cerr_samples_alts_vector(samples_alts_v);

    // Write population.
    write_vcf(contig_names, variants, samples_alts_v, options.vcf_filename_out);
}

// ----------------------------------------------------------------------------
// Function main()
// ----------------------------------------------------------------------------

int main(int argc, char const ** argv)
{
    seqan::ArgumentParser parser;
    app_options options;

    options.setup(parser);

    seqan::ArgumentParser::ParseResult res = parse(parser, argc, argv);

    if (res != seqan::ArgumentParser::PARSE_OK)
        return res;

    options.parse(parser);

    try
    {
        switch (options.n_ploidy)
        {
            case 2:
                run<2>(options);
                break;
            case 3:
                run<3>(options);
                break;
            case 4:
                run<4>(options);
                break;
            case 5:
                run<5>(options);
                break;
            case 6:
                run<6>(options);
                break;
            case 7:
                run<7>(options);
                break;
            case 8:
                run<8>(options);
                break;
            default:
                throw seqan::RuntimeError("Unsupported ploidy");
        }
    }
    catch (seqan::Exception const & e)
    {
        std::cerr << "ERROR: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
