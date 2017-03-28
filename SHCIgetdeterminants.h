#ifndef SHCI_GETDETERMINANTS_H
#define SHCI_GETDETERMINANTS_H
#include <vector>
#include <Eigen/Dense>
#include <set>
#include <list>
#include <tuple>
#include <map>

using namespace std;
using namespace Eigen;
class Determinant;
class HalfDet;
class oneInt;
class twoInt;
class twoIntHeatBath;
class twoIntHeatBathSHM;
class schedule;

namespace SHCIgetdeterminants {
  void getDeterminantsDeterministicPT(Determinant& d, double epsilon, CItype ci1, CItype ci2, oneInt& int1, twoInt& int2, twoIntHeatBathSHM& I2hb, vector<int>& irreps, double coreE, double E0, std::vector<Determinant>& dets, std::vector<CItype>& numerator, std::vector<double>& energy, schedule& schd, int Nmc, int nelec) ;

  void getDeterminantsStochastic(Determinant& d, double epsilon, CItype ci1, CItype ci2, oneInt& int1, twoInt& int2, twoIntHeatBathSHM& I2hb, vector<int>& irreps, double coreE, double E0, std::vector<Determinant>& dets, std::vector<CItype>& numerator1, std::vector<double>& numerator2, std::vector<double>& energy, schedule& schd, int Nmc, int nelec) ;

  void getDeterminantsVariational(Determinant& d, double epsilon, CItype ci1, CItype ci2, oneInt& int1, twoInt& int2, twoIntHeatBathSHM& I2hb, vector<int>& irreps, double coreE, double E0, std::vector<Determinant>& dets, schedule& schd, int Nmc, int nelec) ;

  void getDeterminantsStochastic2Epsilon(Determinant& d, double epsilon, double epsilonLarge, CItype ci1, CItype ci2, oneInt& int1, twoInt& int2, twoIntHeatBathSHM& I2hb, vector<int>& irreps, double coreE, double E0, std::vector<Determinant>& dets, std::vector<CItype>& numerator1A, vector<double>& numerator2A, vector<char>& present, std::vector<double>& energy, schedule& schd, int Nmc, int nelec);

  void getDeterminantsDeterministicPTWithSOC(Determinant det, int det_ind, double epsilon1, CItype ci1, double epsilon2, CItype ci2, oneInt& int1, twoInt& int2, twoIntHeatBathSHM& I2hb, vector<int>& irreps, double coreE, std::vector<Determinant>& dets, std::vector<CItype>& numerator1, std::vector<CItype>& numerator2, std::vector<double>& energy, schedule& schd, int nelec);

  void getDeterminantsDeterministicPTKeepRefDets(Determinant det, int det_ind, double epsilon, CItype ci, oneInt& int1, twoInt& int2, twoIntHeatBathSHM& I2hb, vector<int>& irreps, double coreE, double E0, std::vector<Determinant>& dets, std::vector<CItype>& numerator, std::vector<double>& energy, std::vector<int>& var_indices, std::vector<size_t>& orbDifference, schedule& schd, int nelec);

  
  
}

#endif
