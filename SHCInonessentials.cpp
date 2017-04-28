#include "Determinants.h"
#include "SHCIbasics.h"
#include "SHCInonessentials."
#include "input.h"
#include "integral.h"
#include <vector>
#include "math.h"
#include "Hmult.h"
#include <tuple>
#include <map>
#include "Davidson.h"
#include "boost/format.hpp"
#include <fstream>
#include <boost/serialization/serialization.hpp>
#include <boost/serialization/map.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/serialization/set.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>

#ifndef SERIAL
#include <boost/mpi/environment.hpp>
#include <boost/mpi/communicator.hpp>
#include <boost/mpi.hpp>
#endif
#include "communicate.h"

using namespace std;
using namespace Eigen;
using namespace boost;

void SHCIbasics::DoPerturbativeStochastic2SingleListDoubleEpsilon2OMPTogether(vector<Determinant>& Dets, MatrixXx& ci, double& E0, oneInt& I1, twoInt& I2,
									     twoIntHeatBathSHM& I2HB, vector<int>& irrep, schedule& schd, double coreE, int nelec, int root) {

  boost::mpi::communicator world;
  char file [5000];
  sprintf (file, "output-%d.bkp" , world.rank() );
  std::ofstream ofs;
  if (root == 0)
    ofs.open(file, std::ofstream::out);
  else
    ofs.open(file, std::ofstream::app);

  double epsilon2 = schd.epsilon2;
  schd.epsilon2 = schd.epsilon2Large;
  vector<MatrixXx> vdVector;
  double Psi1Norm;
  double EptLarge = DoPerturbativeDeterministic(Dets, ci, E0, I1, I2, I2HB, irrep, schd, coreE, nelec, root,  vdVector, Psi1Norm);

  schd.epsilon2 = epsilon2;

  int norbs = Determinant::norbs;
  std::vector<Determinant> SortedDets = Dets; std::sort(SortedDets.begin(), SortedDets.end());
  int niter = schd.nPTiter;
  //double eps = 0.001;
  int Nsample = schd.SampleN;
  double AvgenergyEN = 0.0;
  double AverageDen = 0.0;
  int currentIter = 0;
  int sampleSize = 0;
  int num_thrds = omp_get_max_threads();

  double cumulative = 0.0;
  for (int i=0; i<ci.rows(); i++)
    cumulative += abs(ci(i,0));

  std::vector<int> alias; std::vector<double> prob;
  SHCIsampledeterminants::setUpAliasMethod(ci, cumulative, alias, prob);

  double totalPT=0, totalPTLargeEps=0;

  std::vector< std::vector<vector<Determinant> > > hashedDetBeforeMPI   (num_thrds);// vector<Determinant> > >(num_thrds));
  std::vector< std::vector<vector<CItype> > >      hashedNum1BeforeMPI  (num_thrds);// std::vector<vector<double> > >(num_thrds));
  std::vector< std::vector<vector<CItype> > >      hashedNum2BeforeMPI  (num_thrds);// std::vector<vector<double> > >(num_thrds));
  std::vector< std::vector<vector<double> > >      hashedEnergyBeforeMPI(num_thrds);// std::vector<vector<double> > >(num_thrds));
  std::vector< std::vector<vector<char> > >        hashedpresentBeforeMPI(num_thrds);// std::vector<vector<char> > >(num_thrds));

  int AllDistinctSample = 0;
  int Nmc = Nsample*num_thrds;
  std::vector<CItype> allwts(Nmc, 0.0); std::vector<int> allSample(Nmc, -1);

#pragma omp parallel
  {
    for (int iter=0; iter<niter; iter++) {
      std::vector<CItype> wts1(Nsample,0.0); std::vector<int> Sample1(Nsample,-1);
      int distinctSample = 0;

      if (omp_get_thread_num() == 0) {
	std::fill(allSample.begin(), allSample.end(), -1);
	AllDistinctSample = SHCIsampledeterminants::sample_N2_alias(ci, cumulative, allSample, allwts, alias, prob);
      }
#pragma omp barrier
      if (omp_get_thread_num() <  AllDistinctSample%num_thrds)
	distinctSample = AllDistinctSample/num_thrds + 1;
      else
	distinctSample = AllDistinctSample/num_thrds;


      size_t stride = omp_get_thread_num() < AllDistinctSample%num_thrds \
					     ? omp_get_thread_num()*distinctSample \
					     : omp_get_thread_num()*distinctSample + AllDistinctSample%num_thrds;

      for (int i = 0; i < distinctSample; i++) {
	wts1   [i] = allwts   [i + stride];
	Sample1[i] = allSample[i + stride];
      }

      double norm = 0.0;

      std::vector<Determinant> Psi1; std::vector<CItype>  numerator1A; vector<CItype> numerator2A;
      vector<char> present;
      std::vector<double>  det_energy;

      for (int i=0; i<distinctSample; i++) {
	int I = Sample1[i];
	SHCIgetdeterminants::getDeterminantsStochastic2Epsilon(Dets[I], schd.epsilon2/abs(ci(I,0)),
						      schd.epsilon2Large/abs(ci(I,0)), wts1[i],
						      ci(I,0), I1, I2, I2HB, irrep, coreE, E0,
						      Psi1,
						      numerator1A,
						      numerator2A,
						      present,
						      det_energy,
						      schd, Nmc, nelec);
      }

      if(num_thrds >1) {
	hashedDetBeforeMPI[omp_get_thread_num()].resize(num_thrds);
	hashedNum1BeforeMPI[omp_get_thread_num()].resize(num_thrds);
	hashedNum2BeforeMPI[omp_get_thread_num()].resize(num_thrds);
	hashedEnergyBeforeMPI[omp_get_thread_num()].resize(num_thrds);
	hashedpresentBeforeMPI[omp_get_thread_num()].resize(num_thrds);

	for (int thrd=0; thrd<num_thrds; thrd++) {
	  hashedDetBeforeMPI[omp_get_thread_num()][thrd].reserve(Psi1.size()/num_thrds);
	  hashedNum1BeforeMPI[omp_get_thread_num()][thrd].reserve(Psi1.size()/num_thrds);
	  hashedNum2BeforeMPI[omp_get_thread_num()][thrd].reserve(Psi1.size()/num_thrds);
	  hashedEnergyBeforeMPI[omp_get_thread_num()][thrd].reserve(Psi1.size()/num_thrds);
	  hashedpresentBeforeMPI[omp_get_thread_num()][thrd].reserve(Psi1.size()/num_thrds);
	}

	for (int j=0; j<Psi1.size(); j++) {
	  size_t lOrder = Psi1.at(j).getHash();
	  size_t thrd = lOrder%(num_thrds);
	  hashedDetBeforeMPI[omp_get_thread_num()][thrd].push_back(Psi1.at(j));
	  hashedNum1BeforeMPI[omp_get_thread_num()][thrd].push_back(numerator1A.at(j));
	  hashedNum2BeforeMPI[omp_get_thread_num()][thrd].push_back(numerator2A.at(j));
	  hashedEnergyBeforeMPI[omp_get_thread_num()][thrd].push_back(det_energy.at(j));
	  hashedpresentBeforeMPI[omp_get_thread_num()][thrd].push_back(present.at(j));
	}
	Psi1.clear(); numerator1A.clear(); numerator2A.clear(); det_energy.clear(); present.clear();

#pragma omp barrier
	size_t totalSize = 0;
	for (int thrd=0; thrd<num_thrds; thrd++)
	  totalSize += hashedDetBeforeMPI[thrd][omp_get_thread_num()].size();

	Psi1.reserve(totalSize);
	numerator1A.reserve(totalSize);
	numerator2A.reserve(totalSize);
	det_energy.reserve(totalSize);
	present.reserve(totalSize);
	for (int thrd=0; thrd<num_thrds; thrd++) {
	  for (int j=0; j<hashedDetBeforeMPI[thrd][omp_get_thread_num()].size(); j++) {
	    Psi1.push_back(hashedDetBeforeMPI[thrd][omp_get_thread_num()].at(j));
	    numerator1A.push_back(hashedNum1BeforeMPI[thrd][omp_get_thread_num()].at(j));
	    numerator2A.push_back(hashedNum2BeforeMPI[thrd][omp_get_thread_num()].at(j));
	    det_energy.push_back(hashedEnergyBeforeMPI[thrd][omp_get_thread_num()].at(j));
	    present.push_back(hashedpresentBeforeMPI[thrd][omp_get_thread_num()].at(j));
	  }
	  hashedDetBeforeMPI[thrd][omp_get_thread_num()].clear();
	  hashedNum1BeforeMPI[thrd][omp_get_thread_num()].clear();
	  hashedNum2BeforeMPI[thrd][omp_get_thread_num()].clear();
	  hashedEnergyBeforeMPI[thrd][omp_get_thread_num()].clear();
	  hashedpresentBeforeMPI[thrd][omp_get_thread_num()].clear();
	}
      }

      std::vector<Determinant> Psi1copy = Psi1;
      vector<long> detIndex(Psi1.size(), 0);
      vector<long> detIndexcopy(Psi1.size(), 0);
      for (size_t i=0; i<Psi1.size(); i++)
	detIndex[i] = i;
      mergesort(&Psi1copy[0], 0, Psi1.size()-1, &detIndex[0], &( Psi1.operator[](0)), &detIndexcopy[0]);
      detIndexcopy.clear(); Psi1copy.clear();
      reorder(numerator1A, detIndex);
      reorder(numerator2A, detIndex);
      reorder(det_energy, detIndex);
      reorder(present, detIndex);
      detIndex.clear();


      CItype currentNum1A=0.; CItype currentNum2A=0.;
      CItype currentNum1B=0.; CItype currentNum2B=0.;
      vector<Determinant>::iterator vec_it = SortedDets.begin();
      double energyEN = 0.0, energyENLargeEps = 0.0;
      //size_t effNmc = num_thrds*Nmc;

      for (int i=0;i<Psi1.size();) {
	if (Psi1[i] < *vec_it) {
	  currentNum1A += numerator1A[i];
	  currentNum2A += numerator2A[i];
	  if (present[i]) {
	    currentNum1B += numerator1A[i];
	    currentNum2B += numerator2A[i];
	  }

	  if ( i == Psi1.size()-1) {
#ifdef Complex
	    energyEN += (pow(abs(currentNum1A),2)*Nmc/(Nmc-1) - currentNum2A.real())/(det_energy[i] - E0);
	    energyENLargeEps += (pow(abs(currentNum1B),2)*Nmc/(Nmc-1) - currentNum2B.real())/(det_energy[i] - E0);
#else
	    energyEN += (pow(abs(currentNum1A),2)*Nmc/(Nmc-1) - currentNum2A)/(det_energy[i] - E0);
	    energyENLargeEps += (pow(abs(currentNum1B),2)*Nmc/(Nmc-1) - currentNum2B)/(det_energy[i] - E0);
#endif
	  }
	  else if (!(Psi1[i] == Psi1[i+1])) {
#ifdef Complex
	    energyEN += (pow(abs(currentNum1A),2)*Nmc/(Nmc-1) - currentNum2A.real())/(det_energy[i] - E0);
	    energyENLargeEps += (pow(abs(currentNum1B),2)*Nmc/(Nmc-1) - currentNum2B.real())/(det_energy[i] - E0);
#else
	    energyEN += (pow(abs(currentNum1A),2)*Nmc/(Nmc-1) - currentNum2A)/(det_energy[i] - E0);
	    energyENLargeEps += (pow(abs(currentNum1B),2)*Nmc/(Nmc-1) - currentNum2B)/(det_energy[i] - E0);
#endif
	    //energyEN += ( pow(abs(currentNum1A),2)*Nmc/(Nmc-1) - currentNum2A)/(det_energy[i] - E0);
	    //energyENLargeEps += ( pow(abs(currentNum1B),2)*Nmc/(Nmc-1) - currentNum2B)/(det_energy[i] - E0);
	    currentNum1A = 0.;
	    currentNum2A = 0.;
	    currentNum1B = 0.;
	    currentNum2B = 0.;
	  }
	  i++;
	}
	else if (*vec_it <Psi1[i] && vec_it != SortedDets.end())
	  vec_it++;
	else if (*vec_it <Psi1[i] && vec_it == SortedDets.end()) {
	  currentNum1A += numerator1A[i];
	  currentNum2A += numerator2A[i];
	  if (present[i]) {
	    currentNum1B += numerator1A[i];
	    currentNum2B += numerator2A[i];
	  }

	  if ( i == Psi1.size()-1) {
#ifdef Complex
	    energyEN += (pow(abs(currentNum1A),2)*Nmc/(Nmc-1) - currentNum2A.real())/(det_energy[i] - E0);
	    energyENLargeEps += (pow(abs(currentNum1B),2)*Nmc/(Nmc-1) - currentNum2B.real())/(det_energy[i] - E0);
#else
	    energyEN += (pow(abs(currentNum1A),2)*Nmc/(Nmc-1) - currentNum2A)/(det_energy[i] - E0);
	    energyENLargeEps += (pow(abs(currentNum1B),2)*Nmc/(Nmc-1) - currentNum2B)/(det_energy[i] - E0);
#endif
	    //energyEN += ( pow(abs(currentNum1A),2)*Nmc/(Nmc-1) - currentNum2A)/(det_energy[i] - E0);
	    //energyENLargeEps += ( pow(abs(currentNum1B),2)*Nmc/(Nmc-1) - currentNum2B)/(det_energy[i] - E0);
	  }
	  else if (!(Psi1[i] == Psi1[i+1])) {
#ifdef Complex
	    energyEN += (pow(abs(currentNum1A),2)*Nmc/(Nmc-1) - currentNum2A.real())/(det_energy[i] - E0);
	    energyENLargeEps += (pow(abs(currentNum1B),2)*Nmc/(Nmc-1) - currentNum2B.real())/(det_energy[i] - E0);
#else
	    energyEN += (pow(abs(currentNum1A),2)*Nmc/(Nmc-1) - currentNum2A)/(det_energy[i] - E0);
	    energyENLargeEps += (pow(abs(currentNum1B),2)*Nmc/(Nmc-1) - currentNum2B)/(det_energy[i] - E0);
#endif
	    //energyEN += ( pow(abs(currentNum1A),2)*Nmc/(Nmc-1) - currentNum2A)/(det_energy[i] - E0);
	    //energyENLargeEps += ( pow(abs(currentNum1B),2)*Nmc/(Nmc-1) - currentNum2B)/(det_energy[i] - E0);
 	    //energyEN += (currentNum1A*currentNum1A*Nmc/(Nmc-1) - currentNum2A)/(det_energy[i] - E0);
	    //energyENLargeEps += (currentNum1B*currentNum1B*Nmc/(Nmc-1) - currentNum2B)/(det_energy[i] - E0);
	    currentNum1A = 0.;
	    currentNum2A = 0.;
	    currentNum1B = 0.;
	    currentNum2B = 0.;
	  }
	  i++;
	}
	else {
	  if (Psi1[i] == Psi1[i+1])
	    i++;
	  else {
	    vec_it++; i++;
	  }
	}
      }

      totalPT=0; totalPTLargeEps=0;
#pragma omp barrier
#pragma omp critical
      {
	totalPT += energyEN;
	totalPTLargeEps += energyENLargeEps;
      }
#pragma omp barrier

      double finalE = totalPT, finalELargeEps=totalPTLargeEps;
      if (mpigetrank() == 0 && omp_get_thread_num() == 0) {
	AvgenergyEN += -finalE+finalELargeEps+EptLarge; currentIter++;
	cout << finalE<<"  "<<finalELargeEps<<"  "<<EptLarge<<endl;
	std::cout << format("%6i  %14.8f  %s%i %14.8f   %10.2f  %10i")
	  %(currentIter) % (E0-finalE+finalELargeEps+EptLarge) % ("Root") % root % (E0+AvgenergyEN/currentIter) % (getTime()-startofCalc) % AllDistinctSample ;
	cout << endl;
      }
      else if (mpigetrank() != 0 && omp_get_thread_num() == 0) {
	AvgenergyEN += -finalE+finalELargeEps+EptLarge; currentIter++;
	ofs << format("%6i  %14.8f  %s%i %14.8f   %10.2f  %10i")
	  %(currentIter) % (E0-finalE+finalELargeEps+EptLarge) % ("Root") % root % (E0+AvgenergyEN/currentIter) % (getTime()-startofCalc) % AllDistinctSample ;
	ofs << endl;
      }
    }
  }

}
void SHCIbasics::DoPerturbativeStochastic2SingleList(vector<Determinant>& Dets, MatrixXx& ci, double& E0, oneInt& I1, twoInt& I2,
						    twoIntHeatBathSHM& I2HB, vector<int>& irrep, schedule& schd, double coreE, int nelec, int root) {

  boost::mpi::communicator world;
  char file [5000];
  sprintf (file, "output-%d.bkp" , world.rank() );
  //std::ofstream ofs(file);
  std::ofstream ofs;
  if (root == 0)
    ofs.open(file, std::ofstream::out);
  else
    ofs.open(file, std::ofstream::app);

  int norbs = Determinant::norbs;
  std::vector<Determinant> SortedDets = Dets; std::sort(SortedDets.begin(), SortedDets.end());
  int niter = schd.nPTiter;
  //int niter = 1000000;
  //double eps = 0.001;
  int Nsample = schd.SampleN;
  double AvgenergyEN = 0.0;
  double AverageDen = 0.0;
  int currentIter = 0;
  int sampleSize = 0;
  int num_thrds = omp_get_max_threads();

  double cumulative = 0.0;
  for (int i=0; i<ci.rows(); i++)
    cumulative += abs(ci(i,0));

  std::vector<int> alias; std::vector<double> prob;
  SHCIsampledeterminants::setUpAliasMethod(ci, cumulative, alias, prob);
#pragma omp parallel for schedule(dynamic)
  for (int iter=0; iter<niter; iter++) {
    //cout << norbs<<"  "<<nelec<<endl;
    char psiArray[norbs];
    vector<int> psiClosed(nelec,0);
    vector<int> psiOpen(norbs-nelec,0);
    //char psiArray[norbs];
    std::vector<CItype> wts1(Nsample,0.0); std::vector<int> Sample1(Nsample,-1);

    //int Nmc = sample_N2(ci, cumulative, Sample1, wts1);
    int distinctSample = SHCIsampledeterminants::sample_N2_alias(ci, cumulative, Sample1, wts1, alias, prob);
    int Nmc = Nsample;
    double norm = 0.0;

    size_t initSize = 100000;
    std::vector<Determinant> Psi1; std::vector<CItype>  numerator1; std::vector<double> numerator2;
    std::vector<double>  det_energy;
    Psi1.reserve(initSize); numerator1.reserve(initSize); numerator2.reserve(initSize); det_energy.reserve(initSize);
    for (int i=0; i<distinctSample; i++) {
      int I = Sample1[i];
      SHCIgetdeterminants::getDeterminantsStochastic(Dets[I], schd.epsilon2/abs(ci(I,0)), wts1[i], ci(I,0), I1, I2, I2HB, irrep, coreE, E0, Psi1, numerator1, numerator2, det_energy, schd, Nmc, nelec);
    }

    quickSort( &(Psi1[0]), 0, Psi1.size(), &numerator1[0], &numerator2[0], &det_energy);

    CItype currentNum1=0.; double currentNum2=0.;
    vector<Determinant>::iterator vec_it = SortedDets.begin();
    double energyEN = 0.0;

    for (int i=0;i<Psi1.size();) {
      if (Psi1[i] < *vec_it) {
	currentNum1 += numerator1[i];
	currentNum2 += numerator2[i];
	if ( i == Psi1.size()-1)
	  energyEN += (pow(abs(currentNum1),2)*Nmc/(Nmc-1) - currentNum2)/(det_energy[i] - E0);
	else if (!(Psi1[i] == Psi1[i+1])) {
	  energyEN += ( pow(abs(currentNum1),2)*Nmc/(Nmc-1) - currentNum2)/(det_energy[i] - E0);
	  currentNum1 = 0.;
	  currentNum2 = 0.;
	}
	i++;
      }
      else if (*vec_it <Psi1[i] && vec_it != SortedDets.end())
	vec_it++;
      else if (*vec_it <Psi1[i] && vec_it == SortedDets.end()) {
	currentNum1 += numerator1[i];
	currentNum2 += numerator2[i];
	if ( i == Psi1.size()-1)
	  energyEN += (pow(abs(currentNum1),2)*Nmc/(Nmc-1) - currentNum2)/(det_energy[i] - E0);
	//energyEN += (currentNum1*currentNum1*Nmc/(Nmc-1) - currentNum2)/(det_energy[i] - E0);
	else if (!(Psi1[i] == Psi1[i+1])) {
	  energyEN += ( pow(abs(currentNum1),2)*Nmc/(Nmc-1) - currentNum2)/(det_energy[i] - E0);
	  //energyEN += (currentNum1*currentNum1*Nmc/(Nmc-1) - currentNum2)/(det_energy[i] - E0);
	  currentNum1 = 0.;
	  currentNum2 = 0.;
	}
	i++;
      }
      else {
	if (Psi1[i] == Psi1[i+1])
	  i++;
	else {
	  vec_it++; i++;
	}
      }
    }

    sampleSize = distinctSample;

#pragma omp critical
    {
      if (mpigetrank() == 0) {
	AvgenergyEN += energyEN; currentIter++;
	std::cout << format("%6i  %14.8f  %s%i %14.8f   %10.2f  %10i %4i")
	  %(currentIter) % (E0-energyEN) % ("Root") % root % (E0-AvgenergyEN/currentIter) % (getTime()-startofCalc) % sampleSize % (omp_get_thread_num());
	cout << endl;
      }
      else {
	AvgenergyEN += energyEN; currentIter++;
	ofs << format("%6i  %14.8f  %s%i %14.8f   %10.2f  %10i %4i")
	  %(currentIter) % (E0-energyEN) % ("Root") % root % (E0-AvgenergyEN/currentIter) % (getTime()-startofCalc) % sampleSize % (omp_get_thread_num());
	ofs << endl;

      }
    }
  }
  ofs.close();

}

void SHCIbasics::DoBatchDeterministic(vector<Determinant>& Dets, MatrixXx& ci, double& E0, oneInt& I1, twoInt& I2, 
				       twoIntHeatBath& I2HB, vector<int>& irrep, schedule& schd, double coreE, int nelec) {
  int nblocks = schd.nblocks;
  std::vector<int> blockSizes(nblocks,0);
  for (int i=0; i<nblocks; i++) {
    if (i!=nblocks-1)
      blockSizes[i] = Dets.size()/nblocks;
    else
      blockSizes[i] = Dets.size() - (nblocks-1)*Dets.size()/(nblocks);
  }
  int norbs = Determinant::norbs;
  std::vector<Determinant> SortedDets = Dets; std::sort(SortedDets.begin(), SortedDets.end());
  double AvgenergyEN = 0.0;

#pragma omp parallel for schedule(dynamic) 
  for (int inter1 = 0; inter1 < nblocks; inter1++) {

    vector<int> psiClosed(nelec,0); 
    vector<int> psiOpen(norbs-nelec,0);

    std::vector<double> wts1(blockSizes[inter1]);
    std::vector<int> Sample1(blockSizes[inter1]);
    for (int i=0; i<wts1.size(); i++) {
      wts1[i] = ci(i+(inter1)*Dets.size()/nblocks ,0);
      Sample1[i] = i+(inter1)*Dets.size()/nblocks ;
    }

    map<Determinant, pair<double,double> > Psi1ab; 
    for (int i=0; i<Sample1.size(); i++) {
      int I = Sample1[i];
      SHCIbasics::getDeterminants(Dets[I], abs(schd.epsilon2/ci(I,0)), wts1[i], 0.0, I1, I2, I2HB, irrep, coreE, E0, Psi1ab, SortedDets, schd);
    }

    double energyEN = 0.0;
    for (map<Determinant, pair<double, double> >::iterator it = Psi1ab.begin(); it != Psi1ab.end(); it++) {
      it->first.getOpenClosed(psiOpen, psiClosed);
      energyEN += it->second.first*it->second.first/(Energy(psiClosed, nelec, I1, I2, coreE)-E0); 
    }
    

    for (int i=Sample1[Sample1.size()-1]+1; i<Dets.size(); i++) {
      SHCIbasics::getDeterminants(Dets[i], abs(schd.epsilon2/ci(i,0)), 0.0, ci(i,0), I1, I2, I2HB, irrep, coreE, E0, Psi1ab, SortedDets, schd);
      if (i%1000 == 0 && omp_get_thread_num() == 0) cout <<i <<" out of "<<Dets.size()-Sample1.size()<<endl; 
    }

    for (map<Determinant, pair<double, double> >::iterator it = Psi1ab.begin(); it != Psi1ab.end(); it++) {
      it->first.getOpenClosed(psiOpen, psiClosed);
      energyEN += 2.*it->second.first*it->second.second/(Energy(psiClosed, nelec, I1, I2, coreE)-E0); 
    }

#pragma omp critical 
    {
      AvgenergyEN += energyEN;
      
      std::cout << format("%6i  %14.8f   %10.2f  %10i %4i") 
	%(inter1) % (E0-AvgenergyEN) % (getTime()-startofCalc) % inter1 % (omp_get_thread_num());
      cout << endl;
    }
  }
  
  {
    std::cout <<"FINAL ANSWER "<<endl;
    std::cout << format("%4i  %14.8f   %10.2f  %10i %4i") 
      %(nblocks) % (E0-AvgenergyEN) % (getTime()-startofCalc) % nblocks % (0);
    cout << endl;
  }

}



void SHCIbasics::DoPerturbativeStochastic2(vector<Determinant>& Dets, MatrixXx& ci, double& E0, oneInt& I1, twoInt& I2, 
					   twoIntHeatBath& I2HB, vector<int>& irrep, schedule& schd, double coreE, int nelec) {

  cout << "This function is most likely broken, dont use it. Use the single list method instead!!!"<<endl;
  exit(0);
  boost::mpi::communicator world;
  char file [5000];
  sprintf (file, "output-%d.bkp" , world.rank() );
  std::ofstream ofs(file);

    int norbs = Determinant::norbs;
    std::vector<Determinant> SortedDets = Dets; std::sort(SortedDets.begin(), SortedDets.end());
    int niter = 10000;
    //double eps = 0.001;
    int Nsample = schd.SampleN;
    double AvgenergyEN = 0.0;
    double AverageDen = 0.0;
    int currentIter = 0;
    int sampleSize = 0;
    int num_thrds = omp_get_max_threads();

    double cumulative = 0.0;
    for (int i=0; i<ci.rows(); i++)
      cumulative += abs(ci(i,0));

#pragma omp parallel for schedule(dynamic) 
    for (int iter=0; iter<niter; iter++) {
      //cout << norbs<<"  "<<nelec<<endl;
      char psiArray[norbs]; 
      vector<int> psiClosed(nelec,0); 
      vector<int> psiOpen(norbs-nelec,0);
      //char psiArray[norbs];
      std::vector<double> wts1(Nsample,0.0), wts2(Nsample,0.0); std::vector<int> Sample1(Nsample,-1), Sample2(Nsample,-1);
      //wts1.reserve(Nsample); wts2.reserve(Nsample); Sample1.reserve(Nsample); Sample2.reserve(Nsample);
      

      sample_N2(ci, cumulative, Sample1, wts1);
      sample_N2(ci, cumulative, Sample2, wts2);

      double norm = 0.0;
      for (int i=0; i<Sample1.size(); i++) {
	double normi = 0.0;
	for (int j=0; j<Sample2.size(); j++)
	  if (Sample2[j] == Sample1[i]) normi += wts1[i]*wts2[j];
	norm += normi;
      }
      
      map<Determinant, pair<double,double> > Psi1ab; 
      for (int i=0; i<Sample1.size(); i++) {
	int I = Sample1[i];
	std::vector<int>::iterator it = find(Sample2.begin(), Sample2.end(), I);
	if (it != Sample2.end())
	  SHCIbasics::getDeterminants(Dets[I], abs(schd.epsilon2/ci(I,0)), wts1[i], wts2[ distance(Sample2.begin(), it) ], I1, I2, I2HB, irrep, coreE, E0, Psi1ab, SortedDets, schd);
	else
	  SHCIbasics::getDeterminants(Dets[I], abs(schd.epsilon2/ci(I,0)), wts1[i], 0.0, I1, I2, I2HB, irrep, coreE, E0, Psi1ab, SortedDets, schd);
      }

      for (int i=0; i<Sample2.size(); i++) {
	int I = Sample2[i];
	std::vector<int>::iterator it = find(Sample1.begin(), Sample1.end(), I);
	if (it == Sample1.end())
	  SHCIbasics::getDeterminants(Dets[I], abs(schd.epsilon2/ci(I,0)), 0.0, wts2[i], I1, I2, I2HB, irrep, coreE, E0, Psi1ab, SortedDets, schd);
      }


      double energyEN = 0.0;
      for (map<Determinant, pair<double, double> >::iterator it = Psi1ab.begin(); it != Psi1ab.end(); it++) {
	it->first.getOpenClosed(psiOpen, psiClosed);
	energyEN += it->second.first*it->second.second/(Energy(psiClosed, nelec, I1, I2, coreE)-E0); 
      }
      sampleSize = Sample1.size();
      AverageDen += norm;
#pragma omp critical 
      {
	if (mpigetrank() == 0) {
	  AvgenergyEN += energyEN; currentIter++;
	  std::cout << format("%6i  %14.8f  %14.8f %14.8f   %10.2f  %10i %4i") 
	    %(currentIter) % (E0-energyEN) % (norm) % (E0-AvgenergyEN/currentIter) % (getTime()-startofCalc) % sampleSize % (omp_get_thread_num());
	  cout << endl;
	}
	else {
	  AvgenergyEN += energyEN; currentIter++;
	  ofs << format("%6i  %14.8f  %14.8f %14.8f   %10.2f  %10i %4i") 
	    %(currentIter) % (E0-energyEN) % (norm) % (E0-AvgenergyEN/AverageDen) % (getTime()-startofCalc) % sampleSize % (omp_get_thread_num());
	  ofs << endl;

	}
      }
    }
    ofs.close();

}


void SHCIbasics::DoPerturbativeStochastic(vector<Determinant>& Dets, MatrixXx& ci, double& E0, oneInt& I1, twoInt& I2, 
					   twoIntHeatBath& I2HB, vector<int>& irrep, schedule& schd, double coreE, int nelec) {

  boost::mpi::communicator world;
  char file [5000];
  sprintf (file, "output-%d.bkp" , world.rank() );
  std::ofstream ofs(file);

    int norbs = Determinant::norbs;
    std::vector<Determinant> SortedDets = Dets; std::sort(SortedDets.begin(), SortedDets.end());
    int niter = 10000;
    //double eps = 0.001;
    double AvgenergyEN = 0.0;
    double AverageDen = 0.0;
    int currentIter = 0;
    int sampleSize = 0;
    int num_thrds = omp_get_max_threads();

#pragma omp parallel for schedule(dynamic) 
    for (int iter=0; iter<niter; iter++) {
      //cout << norbs<<"  "<<nelec<<endl;
      char psiArray[norbs]; 
      vector<int> psiClosed(nelec,0); 
      vector<int> psiOpen(norbs-nelec,0);
      //char psiArray[norbs];
      std::vector<double> wts1, wts2; std::vector<int> Sample1, Sample2;
      wts1.reserve(1000); wts2.reserve(1000); Sample1.reserve(1000); Sample2.reserve(1000);
      
      Sample1.resize(0); wts1.resize(0); Sample2.resize(0); wts2.resize(0);
      sample_round(ci, schd.eps, Sample1, wts1);
      sample_round(ci, schd.eps, Sample2, wts2);

      double norm = 0.0;
      for (int i=0; i<Sample1.size(); i++) {
	for (int j=0; j<Sample2.size(); j++)
	  if (Sample2[j] == Sample1[i]) {
	    norm += wts1[i]*wts2[j];
	    break;
	  }
      }
      
      map<Determinant, pair<double,double> > Psi1ab; 
      for (int i=0; i<Sample1.size(); i++) {
	int I = Sample1[i];
	//SHCIbasics::getDeterminants(Dets[I], abs(schd.epsilon2/ci(I,0)), wts1[i], 0.0, I1, I2, I2HB, irrep, coreE, E0, Psi1ab, SortedDets, schd);
	std::vector<int>::iterator it = find(Sample2.begin(), Sample2.end(), I);
	if (it != Sample2.end())
	  SHCIbasics::getDeterminants(Dets[I], abs(schd.epsilon2/ci(I,0)), wts1[i], wts2[ distance(Sample2.begin(), it) ], I1, I2, I2HB, irrep, coreE, E0, Psi1ab, SortedDets, schd);
	else
	  SHCIbasics::getDeterminants(Dets[I], abs(schd.epsilon2/ci(I,0)), wts1[i], 0.0, I1, I2, I2HB, irrep, coreE, E0, Psi1ab, SortedDets, schd);
      }


      for (int i=0; i<Sample2.size(); i++) {
	int I = Sample2[i];
	std::vector<int>::iterator it = find(Sample1.begin(), Sample1.end(), I);
	if (it == Sample1.end())
	  SHCIbasics::getDeterminants(Dets[I], abs(schd.epsilon2/ci(I,0)), 0.0, wts2[i], I1, I2, I2HB, irrep, coreE, E0, Psi1ab, SortedDets, schd);
      }


      double energyEN = 0.0;
      for (map<Determinant, pair<double, double> >::iterator it = Psi1ab.begin(); it != Psi1ab.end(); it++) {
	it->first.getOpenClosed(psiOpen, psiClosed);
	energyEN += it->second.first*it->second.second/(Energy(psiClosed, nelec, I1, I2, coreE)-E0); 
      }
      sampleSize = Sample1.size();

#pragma omp critical 
      {
	if (mpigetrank() == 0) {
	  AvgenergyEN += energyEN; currentIter++;
	  std::cout << format("%6i  %14.8f  %14.8f %14.8f   %10.2f  %10i %4i") 
	    %(currentIter) % (E0-energyEN) % (norm) % (E0-AvgenergyEN/currentIter) % (getTime()-startofCalc) % sampleSize % (omp_get_thread_num());
	  cout << endl;
	}
	else {
	  AvgenergyEN += energyEN; currentIter++;
	  ofs << format("%6i  %14.8f  %14.8f %14.8f   %10.2f  %10i %4i") 
	    %(currentIter) % (E0-energyEN) % (norm) % (E0-AvgenergyEN/AverageDen) % (getTime()-startofCalc) % sampleSize % (omp_get_thread_num());
	  ofs << endl;

	}

	//AverageDen += norm;
	//AvgenergyEN += energyEN; currentIter++;
	//std::cout << format("%6i  %14.8f  %14.8f  %14.8f   %10.2f  %10i %4i") 
	//%(currentIter) % (E0-energyEN) % (norm) % (E0-AvgenergyEN/currentIter) % (getTime()-startofCalc) % sampleSize % (omp_get_thread_num());
	//cout << endl;
	//%(currentIter) % (E0-AvgenergyEN/currentIter) % (norm) % (E0-AvgenergyEN/AverageDen) % (getTime()-startofCalc) % sampleSize % (omp_get_thread_num());
	//cout << endl;
      }
    }
    ofs.close();
}


void SHCIbasics::DoPerturbativeStochasticSingleList(vector<Determinant>& Dets, MatrixXx& ci, double& E0, oneInt& I1, twoInt& I2, 
						     twoIntHeatBath& I2HB, vector<int>& irrep, schedule& schd, double coreE, int nelec) {

  boost::mpi::communicator world;
  char file [5000];
  sprintf (file, "output-%d.bkp" , world.rank() );
  std::ofstream ofs(file);

    int norbs = Determinant::norbs;
    std::vector<Determinant> SortedDets = Dets; std::sort(SortedDets.begin(), SortedDets.end());
    int niter = 10000;
    //double eps = 0.001;
    double AvgenergyEN = 0.0;
    double AverageDen = 0.0;
    int currentIter = 0;
    int sampleSize = 0;
    int num_thrds = omp_get_max_threads();

#pragma omp parallel for schedule(dynamic) 
    for (int iter=0; iter<niter; iter++) {
      //cout << norbs<<"  "<<nelec<<endl;
      char psiArray[norbs]; 
      vector<int> psiClosed(nelec,0); 
      vector<int> psiOpen(norbs-nelec,0);
      //char psiArray[norbs];
      std::vector<double> wts1; std::vector<int> Sample1;
      wts1.reserve(1000);  Sample1.reserve(1000);
      
      Sample1.resize(0); wts1.resize(0);
      sample_round(ci, schd.eps, Sample1, wts1);
      
      map<Determinant, pair<double,double> > Psi1ab; 
      for (int i=0; i<Sample1.size(); i++) {
	int I = Sample1[i];
	SHCIbasics::getDeterminants(Dets[I], abs(schd.epsilon2/ci(I,0)), wts1[i], ci(I,0), I1, I2, I2HB, irrep, coreE, E0, Psi1ab, SortedDets, schd);
      }



      double energyEN = 0.0;
      for (map<Determinant, pair<double, double> >::iterator it = Psi1ab.begin(); it != Psi1ab.end(); it++) {
	it->first.getOpenClosed(psiOpen, psiClosed);
	energyEN += (it->second.first*it->second.first - it->second.second)/(Energy(psiClosed, nelec, I1, I2, coreE)-E0); 
      }
      sampleSize = Sample1.size();

#pragma omp critical 
      {
	if (mpigetrank() == 0) {
	  AvgenergyEN += energyEN; currentIter++;
	  std::cout << format("%6i  %14.8f  %14.8f %14.8f   %10.2f  %10i %4i") 
	    %(currentIter) % (E0-energyEN) % (1.0) % (E0-AvgenergyEN/currentIter) % (getTime()-startofCalc) % sampleSize % (omp_get_thread_num());
	  cout << endl;
	}
	else {
	  AvgenergyEN += energyEN; currentIter++;
	  ofs << format("%6i  %14.8f  %14.8f %14.8f   %10.2f  %10i %4i") 
	    %(currentIter) % (E0-energyEN) % (1.0) % (E0-AvgenergyEN/AverageDen) % (getTime()-startofCalc) % sampleSize % (omp_get_thread_num());
	  ofs << endl;

	}

	//AverageDen += norm;
	//AvgenergyEN += energyEN; currentIter++;
	//std::cout << format("%6i  %14.8f  %14.8f  %14.8f   %10.2f  %10i %4i") 
	//%(currentIter) % (E0-energyEN) % (norm) % (E0-AvgenergyEN/currentIter) % (getTime()-startofCalc) % sampleSize % (omp_get_thread_num());
	//cout << endl;
	//%(currentIter) % (E0-AvgenergyEN/currentIter) % (norm) % (E0-AvgenergyEN/AverageDen) % (getTime()-startofCalc) % sampleSize % (omp_get_thread_num());
	//cout << endl;
      }
    }
    ofs.close();
}


class sort_indices
{
private:
  Determinant* mparr;
public:
  sort_indices(Determinant* parr) : mparr(parr) {}
  bool operator()(int i, int j) const { return mparr[i]<mparr[j]; }
};

//this function is complicated because I wanted to make it general enough that deterministicperturbative and stochasticperturbative could use the same function
//in stochastic perturbative each determinant in Psi1 can come from the first replica of Psi0 or the second replica of Psi0. that is why you have a pair of doubles associated with each det in Psi1 and we pass ci1 and ci2 which are the coefficients of d in replica 1 and replica2 of Psi0.
void SHCIbasics::getDeterminants(Determinant& d, double epsilon, double ci1, double ci2, oneInt& int1, twoInt& int2, twoIntHeatBath& I2hb, vector<int>& irreps, double coreE, double E0, std::map<Determinant, pair<double,double> >& Psi1, std::vector<Determinant>& Psi0, schedule& schd, int Nmc) {

  int norbs = d.norbs;
  int open[norbs], closed[norbs]; char detArray[norbs], diArray[norbs];
  int nclosed = d.getOpenClosed(open, closed);
  int nopen = norbs-nclosed;
  d.getRepArray(detArray);
  
  std::map<Determinant, pair<double,double> >::iterator det_it;
  for (int ia=0; ia<nopen*nclosed; ia++){
    int i=ia/nopen, a=ia%nopen;
    if (open[a]/2 > schd.nvirt+nclosed/2) continue; //dont occupy above a certain orbital
    if (irreps[closed[i]/2] != irreps[open[a]/2]) continue;
    double integral = Hij_1Excite(closed[i],open[a],int1,int2, detArray, norbs);
    if (fabs(integral) > epsilon ) {
      Determinant di = d;
      di.setocc(open[a], true); di.setocc(closed[i],false);
      if (!(binary_search(Psi0.begin(), Psi0.end(), di))) {
	det_it = Psi1.find(di);

	if (schd.singleList && schd.SampleN != -1) {
	  if (det_it == Psi1.end()) Psi1[di] = make_pair(integral*ci1, integral*integral*ci1*(ci1*Nmc/(Nmc-1)-ci2));
	  else {det_it->second.first +=integral*ci1;det_it->second.second += integral*integral*ci1*(ci1*Nmc/(Nmc-1)-ci2);}
	}
	else if (schd.singleList && schd.SampleN == -1) {
	  if (det_it == Psi1.end()) Psi1[di] = make_pair(integral*ci1, integral*integral*ci2*ci1*(ci1/ci2-1.));
	  else {det_it->second.first +=integral*ci1;det_it->second.second += integral*integral*ci2*ci1*(ci1/ci2-1.);}
	}
	else {
	  if (det_it == Psi1.end()) Psi1[di] = make_pair(integral*ci1, integral*ci2);
	  else {det_it->second.first +=integral*ci1;det_it->second.second +=integral*ci2;}
	}
      }
    }
  }
  
  if (fabs(int2.maxEntry) <epsilon) return;

  //#pragma omp parallel for schedule(dynamic)
  for (int ij=0; ij<nclosed*nclosed; ij++) {

    int i=ij/nclosed, j = ij%nclosed;
    if (i<=j) continue;
    int I = closed[i]/2, J = closed[j]/2;
    std::pair<int,int> IJpair(max(I,J), min(I,J));
    std::map<std::pair<int,int>, std::multimap<double, std::pair<int,int>, compAbs > >::iterator ints = closed[i]%2==closed[j]%2 ? I2hb.sameSpin.find(IJpair) : I2hb.oppositeSpin.find(IJpair);

    //THERE IS A BUG IN THE CODE WHEN USING HEATBATH INTEGRALS
    if (true && (ints != I2hb.sameSpin.end() && ints != I2hb.oppositeSpin.end())) { //we have this pair stored in heat bath integrals
      for (std::multimap<double, std::pair<int,int>,compAbs >::reverse_iterator it=ints->second.rbegin(); it!=ints->second.rend(); it++) {
	if (fabs(it->first) <epsilon) break; //if this is small then all subsequent ones will be small
	int a = 2* it->second.first + closed[i]%2, b= 2*it->second.second+closed[j]%2;
	if (a/2 > schd.nvirt+nclosed/2 || b/2 >schd.nvirt+nclosed/2) continue; //dont occupy above a certain orbital
	//cout << a/2<<"  "<<schd.nvirt<<"  "<<nclosed/2<<endl;
	if (!(d.getocc(a) || d.getocc(b))) {
	  Determinant di = d;
	  di.setocc(a, true), di.setocc(b, true), di.setocc(closed[i],false), di.setocc(closed[j], false);	    
	  if (!(binary_search(Psi0.begin(), Psi0.end(), di))) {
	    double sgn = 1.0;
	    {
	      int A = (closed[i]), B = closed[j], I= a, J = b; 
	      sgn = parity(detArray,norbs,A)*parity(detArray,norbs,I)*parity(detArray,norbs,B)*parity(detArray,norbs,J);
	      if (B > J) sgn*=-1 ;
	      if (I > J) sgn*=-1 ;
	      if (I > B) sgn*=-1 ;
	      if (A > J) sgn*=-1 ;
	      if (A > B) sgn*=-1 ;
	      if (A > I) sgn*=-1 ;
	    }

	    det_it = Psi1.find(di);

	    if (schd.singleList && schd.SampleN != -1) {
	      if (det_it == Psi1.end()) Psi1[di] = make_pair(it->first*sgn*ci1, it->first*it->first*ci1*(ci1*Nmc/(Nmc-1)-ci2));
	      else {det_it->second.first +=it->first*sgn*ci1;det_it->second.second += it->first*it->first*ci1*(ci1*Nmc/(Nmc-1)-ci2);}
	    }
	    else if (schd.singleList && schd.SampleN == -1) {
	      if (det_it == Psi1.end()) Psi1[di] = make_pair(it->first*sgn*ci1, it->first*it->first*ci1*(ci1-ci2));
	      else {det_it->second.first +=it->first*sgn*ci1;det_it->second.second += it->first*it->first*ci1*(ci1-ci2);}
	    }
	    else {
	      if (det_it == Psi1.end()) Psi1[di] = make_pair(it->first*sgn*ci1, it->first*sgn*ci2);
	      else {det_it->second.first += it->first*sgn*ci1; det_it->second.second += it->first*sgn*ci2;}
	    }
	  }
	}
      }
    }
  }
  return;
}



//this function is complicated because I wanted to make it general enough that deterministicperturbative and stochasticperturbative could use the same function
//in stochastic perturbative each determinant in Psi1 can come from the first replica of Psi0 or the second replica of Psi0. that is why you have a pair of doubles associated with each det in Psi1 and we pass ci1 and ci2 which are the coefficients of d in replica 1 and replica2 of Psi0.
void SHCIbasics::getDeterminants(Determinant& d, double epsilon, double ci1, double ci2, oneInt& int1, twoInt& int2, twoIntHeatBath& I2hb, vector<int>& irreps, double coreE, double E0, std::map<Determinant, std::tuple<double,double,double> >& Psi1, std::vector<Determinant>& Psi0, schedule& schd, int Nmc, int nelec) {

  int norbs = d.norbs;
  vector<int> closed(nelec,0); 
  vector<int> open(norbs-nelec,0);
  d.getOpenClosed(open, closed);
  int nclosed = nelec;
  char detArray[norbs], diArray[norbs];
  int nopen = norbs-nclosed;
  d.getRepArray(detArray);


  double Energyd = Energy(closed, nclosed, int1, int2, coreE);
  
  std::map<Determinant, std::tuple<double,double, double> >::iterator det_it;
  for (int ia=0; ia<nopen*nclosed; ia++){
    int i=ia/nopen, a=ia%nopen;
    if (open[a]/2 > schd.nvirt+nclosed/2) continue; //dont occupy above a certain orbital
    if (irreps[closed[i]/2] != irreps[open[a]/2]) continue;

    //double integral = Hij_1Excite(closed[i],open[a],int1,int2, detArray, norbs);
    double integral = d.Hij_1Excite(closed[i], open[a], int1, int2);

    if (fabs(integral) > epsilon ) {
      Determinant di = d;
      di.setocc(open[a], true); di.setocc(closed[i],false);
      if (!(binary_search(Psi0.begin(), Psi0.end(), di))) {
	det_it = Psi1.find(di);

	if (schd.singleList && schd.SampleN != -1) {
	  if (det_it == Psi1.end()) {
	    double E = EnergyAfterExcitation(closed, nclosed, int1, int2, coreE, i, open[a], Energyd);
	    Psi1[di] = std::tuple<double, double, double>(integral*ci1, integral*integral*ci1*(ci1*Nmc/(Nmc-1)-ci2), E);
	  }
	  else {std::get<0>(det_it->second) +=integral*ci1; std::get<1>(det_it->second) += integral*integral*ci1*(ci1*Nmc/(Nmc-1)-ci2);}
	}
      }
    }
  }
  
  if (fabs(int2.maxEntry) <epsilon) return;

  //#pragma omp parallel for schedule(dynamic)
  for (int ij=0; ij<nclosed*nclosed; ij++) {

    int i=ij/nclosed, j = ij%nclosed;
    if (i<=j) continue;
    int I = closed[i]/2, J = closed[j]/2;
    std::pair<int,int> IJpair(max(I,J), min(I,J));
    std::map<std::pair<int,int>, std::multimap<double, std::pair<int,int>, compAbs > >::iterator ints = closed[i]%2==closed[j]%2 ? I2hb.sameSpin.find(IJpair) : I2hb.oppositeSpin.find(IJpair);

    if (true && (ints != I2hb.sameSpin.end() && ints != I2hb.oppositeSpin.end())) { //we have this pair stored in heat bath integrals
      for (std::multimap<double, std::pair<int,int>,compAbs >::reverse_iterator it=ints->second.rbegin(); it!=ints->second.rend(); it++) {
	if (fabs(it->first) <epsilon) break; //if this is small then all subsequent ones will be small
	int a = 2* it->second.first + closed[i]%2, b= 2*it->second.second+closed[j]%2;
	if (a/2 > schd.nvirt+nclosed/2 || b/2 >schd.nvirt+nclosed/2) continue; //dont occupy above a certain orbital


	//cout << a/2<<"  "<<schd.nvirt<<"  "<<nclosed/2<<endl;
	if (!(d.getocc(a) || d.getocc(b))) {
	  Determinant di = d;
	  di.setocc(a, true), di.setocc(b, true), di.setocc(closed[i],false), di.setocc(closed[j], false);	    
	  if (!(binary_search(Psi0.begin(), Psi0.end(), di))) {

	    double sgn = 1.0;
	    di.parity(a, b, closed[i], closed[j], sgn);

	    det_it = Psi1.find(di);


	    if (schd.singleList && schd.SampleN != -1) {
	      if (det_it == Psi1.end()) {
		double E = EnergyAfterExcitation(closed, nclosed, int1, int2, coreE, i, a, j, b, Energyd);	    
		Psi1[di] = std::tuple<double,double,double>(it->first*sgn*ci1, it->first*it->first*ci1*(ci1*Nmc/(Nmc-1)-ci2),E);
	      }
	      else {std::get<0>(det_it->second) +=it->first*sgn*ci1;std::get<1>(det_it->second) += it->first*it->first*ci1*(ci1*Nmc/(Nmc-1)-ci2);}
	    }
	  }
	}
      }
    }
  }
  return;
}

//this function is complicated because I wanted to make it general enough that deterministicperturbative and stochasticperturbative could use the same function
//in stochastic perturbative each determinant in Psi1 can come from the first replica of Psi0 or the second replica of Psi0. that is why you have a pair of doubles associated with each det in Psi1 and we pass ci1 and ci2 which are the coefficients of d in replica 1 and replica2 of Psi0.
void SHCIbasics::getDeterminants2Epsilon(Determinant& d, double epsilon, double epsilonLarge, double ci1, double ci2, oneInt& int1, twoInt& int2, twoIntHeatBath& I2hb, vector<int>& irreps, double coreE, double E0, std::map<Determinant, std::tuple<double, double,double,double,double> >& Psi1, std::vector<Determinant>& Psi0, schedule& schd, int Nmc, int nelec) {

  int norbs = d.norbs;
  vector<int> closed(nelec,0); 
  vector<int> open(norbs-nelec,0);
  d.getOpenClosed(open, closed);
  int nclosed = nelec;
  char detArray[norbs], diArray[norbs];
  int nopen = norbs-nclosed;
  d.getRepArray(detArray);


  double Energyd = Energy(closed, nclosed, int1, int2, coreE);
  
  std::map<Determinant, std::tuple<double, double, double,double, double> >::iterator det_it;
  for (int ia=0; ia<nopen*nclosed; ia++){
    int i=ia/nopen, a=ia%nopen;
    if (open[a]/2 > schd.nvirt+nclosed/2) continue; //dont occupy above a certain orbital
    if (irreps[closed[i]/2] != irreps[open[a]/2]) continue;
    double integral = Hij_1Excite(closed[i],open[a],int1,int2, detArray, norbs);
    if (fabs(integral) > epsilon ) {
      Determinant di = d;
      di.setocc(open[a], true); di.setocc(closed[i],false);
      if (!(binary_search(Psi0.begin(), Psi0.end(), di))) {
	det_it = Psi1.find(di);

	if (schd.singleList && schd.SampleN != -1) {
	  if (det_it == Psi1.end()) {
	    double E = EnergyAfterExcitation(closed, nclosed, int1, int2, coreE, i, open[a], Energyd);
	    Psi1[di] = std::tuple<double, double, double, double, double>(integral*ci1, integral*integral*ci1*(ci1*Nmc/(Nmc-1)-ci2), E,0.0,0.0);
	  }
	  else {std::get<0>(det_it->second) +=integral*ci1; std::get<1>(det_it->second) += integral*integral*ci1*(ci1*Nmc/(Nmc-1)-ci2);}
	}

	if (fabs(integral) > epsilonLarge ) {
	  det_it = Psi1.find(di);
	  std::get<3>(det_it->second) +=integral*ci1; std::get<4>(det_it->second) += integral*integral*ci1*(ci1*Nmc/(Nmc-1)-ci2);
	}

      }
    }
  }
  
  if (fabs(int2.maxEntry) <epsilon) return;

  //#pragma omp parallel for schedule(dynamic)
  for (int ij=0; ij<nclosed*nclosed; ij++) {

    int i=ij/nclosed, j = ij%nclosed;
    if (i<=j) continue;
    int I = closed[i]/2, J = closed[j]/2;
    std::pair<int,int> IJpair(max(I,J), min(I,J));
    std::map<std::pair<int,int>, std::multimap<double, std::pair<int,int>, compAbs > >::iterator ints = closed[i]%2==closed[j]%2 ? I2hb.sameSpin.find(IJpair) : I2hb.oppositeSpin.find(IJpair);

    //THERE IS A BUG IN THE CODE WHEN USING HEATBATH INTEGRALS
    if (true && (ints != I2hb.sameSpin.end() && ints != I2hb.oppositeSpin.end())) { //we have this pair stored in heat bath integrals
      for (std::multimap<double, std::pair<int,int>,compAbs >::reverse_iterator it=ints->second.rbegin(); it!=ints->second.rend(); it++) {
	if (fabs(it->first) <epsilon) break; //if this is small then all subsequent ones will be small
	int a = 2* it->second.first + closed[i]%2, b= 2*it->second.second+closed[j]%2;
	if (a/2 > schd.nvirt+nclosed/2 || b/2 >schd.nvirt+nclosed/2) continue; //dont occupy above a certain orbital


	//cout << a/2<<"  "<<schd.nvirt<<"  "<<nclosed/2<<endl;
	if (!(d.getocc(a) || d.getocc(b))) {
	  Determinant di = d;
	  di.setocc(a, true), di.setocc(b, true), di.setocc(closed[i],false), di.setocc(closed[j], false);	    
	  if (!(binary_search(Psi0.begin(), Psi0.end(), di))) {

	    double sgn = 1.0;
	    {
	      int A = (closed[i]), B = closed[j], I= a, J = b; 
	      sgn = parity(detArray,norbs,A)*parity(detArray,norbs,I)*parity(detArray,norbs,B)*parity(detArray,norbs,J);
	      if (B > J) sgn*=-1 ;
	      if (I > J) sgn*=-1 ;
	      if (I > B) sgn*=-1 ;
	      if (A > J) sgn*=-1 ;
	      if (A > B) sgn*=-1 ;
	      if (A > I) sgn*=-1 ;
	    }

	    det_it = Psi1.find(di);


	    if (schd.singleList && schd.SampleN != -1) {
	      if (det_it == Psi1.end()) {
		double E = EnergyAfterExcitation(closed, nclosed, int1, int2, coreE, i, a, j, b, Energyd);	    
		Psi1[di] = std::tuple<double,double,double,double,double>(it->first*sgn*ci1, it->first*it->first*ci1*(ci1*Nmc/(Nmc-1)-ci2),E,0.0,0.0);
	      }
	      else {std::get<0>(det_it->second) +=it->first*sgn*ci1;std::get<1>(det_it->second) += it->first*it->first*ci1*(ci1*Nmc/(Nmc-1)-ci2);}
	    }

	    if (fabs(it->first) > epsilonLarge ) {
	      det_it = Psi1.find(di);
	      std::get<3>(det_it->second) +=it->first*sgn*ci1;std::get<4>(det_it->second) += it->first*it->first*ci1*(ci1*Nmc/(Nmc-1)-ci2);
	    }

	  }
	}
      }
    }
  }
  return;
}



void SHCIbasics::updateConnections(vector<Determinant>& Dets, map<Determinant,int>& SortedDets, int norbs, oneInt& int1, twoInt& int2, double coreE, char* detArray, vector<vector<int> >& connections, vector<vector<double> >& Helements) {
  size_t prevSize = SortedDets.size();
  size_t Norbs = norbs;
  for (size_t i=prevSize; i<Dets.size(); i++) {
    SortedDets[Dets[i]] = i;
    connections[i].push_back(i);
    Helements[i].push_back(Energy(&detArray[i*Norbs], norbs, int1, int2, coreE));
  }

#pragma omp parallel for schedule(dynamic)
  for (size_t x=prevSize; x<Dets.size(); x++) {
    Determinant d = Dets[x];
    int open[norbs], closed[norbs]; 
    int nclosed = d.getOpenClosed(open, closed);
    int nopen = norbs-nclosed;
    
    if (x%10000 == 0) cout <<"update connections "<<x<<" out of "<<Dets.size()-prevSize<<endl;
    //loop over all single excitation and find if they are present in the list
    //on or before the current determinant
    for (int ia=0; ia<nopen*nclosed; ia++){
      int i=ia/nopen, a=ia%nopen;
      Determinant di = d;
      di.setocc(open[a], true); di.setocc(closed[i],false);
      
      map<Determinant, int>::iterator it = SortedDets.find(di);
      if (it != SortedDets.end()) {
	int y = it->second;
	if (y <= x) { //avoid double counting
	  double integral = Hij_1Excite(closed[i],open[a],int1,int2, &detArray[x*Norbs], norbs);
	  if (abs(integral) > 1.e-8) {
	    connections[x].push_back(y);
	    Helements[x].push_back(integral);
	  }
	  //connections[y].push_back(x);
	  //Helements[y].push_back(integral);
	}
      }
    }


    for (int i=0; i<nclosed; i++)
      for (int j=0; j<i; j++) {
	for (int a=0; a<nopen; a++){
	  for (int b=0; b<a; b++){
	    Determinant di = d;
	    di.setocc(open[a], true), di.setocc(open[b], true), di.setocc(closed[i],false), di.setocc(closed[j], false);

	    map<Determinant, int>::iterator it = SortedDets.find(di);
	    if (it != SortedDets.end()) {
	      int y = it->second;
	      if (y <= x) { //avoid double counting
		double integral = Hij_2Excite(closed[i], closed[j], open[a], open[b], int2, &detArray[x*Norbs], norbs);
		if (abs(integral) > 1.e-8) {
		  connections[x].push_back(y);
		  Helements[x].push_back(integral);
		  //cout << x<<"  "<<y<<"  "<<integral<<endl;
		}
		//connections[y].push_back(x);
		//Helements[y].push_back(integral);
	      }
	    }
	  }
	}
      }
  }

  
}
void SHCIbasics::DoPerturbativeStochastic2SingleListDoubleEpsilon2AllTogether(vector<Determinant>& Dets, MatrixXx& ci, double& E0, oneInt& I1, twoInt& I2, 
									  twoIntHeatBathSHM& I2HB, vector<int>& irrep, schedule& schd, double coreE, int nelec, int root) {

  boost::mpi::communicator world;

  double epsilon2 = schd.epsilon2;
  schd.epsilon2 = schd.epsilon2Large;
  double EptLarge = DoPerturbativeDeterministic(Dets, ci, E0, I1, I2, I2HB, irrep, schd, coreE, nelec);

  schd.epsilon2 = epsilon2;

  int norbs = Determinant::norbs;
  std::vector<Determinant> SortedDets = Dets; std::sort(SortedDets.begin(), SortedDets.end());
  int niter = schd.nPTiter;
  //double eps = 0.001;
  int Nsample = schd.SampleN;
  double AvgenergyEN = 0.0;
  double AverageDen = 0.0;
  int currentIter = 0;
  int sampleSize = 0;
  int num_thrds = omp_get_max_threads();
  
  double cumulative = 0.0;
  for (int i=0; i<ci.rows(); i++)
    cumulative += abs(ci(i,0));

  std::vector<int> alias; std::vector<double> prob;
  setUpAliasMethod(ci, cumulative, alias, prob);

  double totalPT=0, totalPTLargeEps=0;

  std::vector<std::vector< std::vector<vector<Determinant> > > > hashedDetBeforeMPI(mpigetsize(), std::vector<std::vector<vector<Determinant> > >(num_thrds));
  std::vector<std::vector< std::vector<vector<Determinant> > > > hashedDetAfterMPI(mpigetsize(), std::vector<std::vector<vector<Determinant> > >(num_thrds));
  std::vector<std::vector< std::vector<vector<double> > > > hashedNum1BeforeMPI(mpigetsize(), std::vector<std::vector<vector<double> > >(num_thrds));
  std::vector<std::vector< std::vector<vector<double> > > > hashedNum1AfterMPI(mpigetsize(), std::vector<std::vector<vector<double> > >(num_thrds));
  std::vector<std::vector< std::vector<vector<double> > > > hashedNum2BeforeMPI(mpigetsize(), std::vector<std::vector<vector<double> > >(num_thrds));
  std::vector<std::vector< std::vector<vector<double> > > > hashedNum2AfterMPI(mpigetsize(), std::vector<std::vector<vector<double> > >(num_thrds));
  std::vector<std::vector< std::vector<vector<double> > > > hashedEnergyBeforeMPI(mpigetsize(), std::vector<std::vector<vector<double> > >(num_thrds));
  std::vector<std::vector< std::vector<vector<double> > > > hashedEnergyAfterMPI(mpigetsize(), std::vector<std::vector<vector<double> > >(num_thrds));
  std::vector<std::vector< std::vector<vector<char> > > > hashedpresentBeforeMPI(mpigetsize(), std::vector<std::vector<vector<char> > >(num_thrds));
  std::vector<std::vector< std::vector<vector<char> > > > hashedpresentAfterMPI(mpigetsize(), std::vector<std::vector<vector<char> > >(num_thrds));

#pragma omp parallel
  {
    for (int iter=0; iter<niter; iter++) {
      std::vector<CItype> wts1(Nsample,0.0); std::vector<int> Sample1(Nsample,-1);
      int distinctSample = sample_N2_alias(ci, cumulative, Sample1, wts1, alias, prob);
      for (int i=0; i<Nsample; i++)
	wts1[i] /= (num_thrds*mpigetsize());
      int Nmc = Nsample;
      double norm = 0.0;
    
      std::vector<Determinant> Psi1; std::vector<CItype>  numerator1A; vector<double> numerator2A;
      vector<char> present;
      std::vector<double>  det_energy;

      for (int i=0; i<distinctSample; i++) {
	int I = Sample1[i];
	SHCIbasics::getDeterminants2Epsilon(Dets[I], schd.epsilon2/abs(ci(I,0)), 
					   schd.epsilon2Large/abs(ci(I,0)), wts1[i], 
					   ci(I,0), I1, I2, I2HB, irrep, coreE, E0, 
					   Psi1, 
					   numerator1A, 
					   numerator2A, 
					   present, 
					   det_energy, 
					   schd, Nmc, nelec);
      }      
      

      std::vector<Determinant> Psi1copy = Psi1;
      vector<long> detIndex(Psi1.size(), 0);
      vector<long> detIndexcopy(Psi1.size(), 0);
      for (size_t i=0; i<Psi1.size(); i++)
	detIndex[i] = i;      
      mergesort(&Psi1copy[0], 0, Psi1.size()-1, &detIndex[0], &( Psi1.operator[](0)), &detIndexcopy[0]);
      detIndexcopy.clear(); Psi1copy.clear();      
      reorder(numerator1A, detIndex);
      reorder(numerator2A, detIndex);
      reorder(det_energy, detIndex);
      reorder(present, detIndex);
      detIndex.clear();

      //quickSort( &(Psi1[0]), 0, Psi1.size(), &numerator1A[0], &numerator2A[0], &det_energy, &present);
      RemoveDetsPresentIn(SortedDets, Psi1, numerator1A, numerator2A, det_energy, present);

      if(mpigetsize() >1 || num_thrds >1) {
	for (int proc=0; proc<mpigetsize(); proc++) {
	  hashedDetBeforeMPI[proc][omp_get_thread_num()].resize(num_thrds);
	  hashedNum1BeforeMPI[proc][omp_get_thread_num()].resize(num_thrds);
	  hashedNum2BeforeMPI[proc][omp_get_thread_num()].resize(num_thrds);
	  hashedEnergyBeforeMPI[proc][omp_get_thread_num()].resize(num_thrds);
	  hashedpresentBeforeMPI[proc][omp_get_thread_num()].resize(num_thrds);
	}
	for (int j=0; j<Psi1.size(); j++) {
	  size_t lOrder = Psi1.at(j).getHash();
	  size_t procThrd = lOrder%(mpigetsize()*num_thrds);
	  int proc = abs(procThrd/num_thrds), thrd = abs(procThrd%num_thrds);
	  hashedDetBeforeMPI[proc][omp_get_thread_num()][thrd].push_back(Psi1.at(j));
	  hashedNum1BeforeMPI[proc][omp_get_thread_num()][thrd].push_back(numerator1A.at(j));
	  hashedNum2BeforeMPI[proc][omp_get_thread_num()][thrd].push_back(numerator2A.at(j));
	  hashedEnergyBeforeMPI[proc][omp_get_thread_num()][thrd].push_back(det_energy.at(j));
	  hashedpresentBeforeMPI[proc][omp_get_thread_num()][thrd].push_back(present.at(j));
	}
	Psi1.clear(); numerator1A.clear(); numerator2A.clear(); det_energy.clear(); present.clear();
	
	//if (mpigetrank() == 0 && omp_get_thread_num() == 0) cout << "#After hash "<<getTime()-startofCalc<<endl;


#pragma omp barrier
	if (omp_get_thread_num()==0) {
	  mpi::all_to_all(world, hashedDetBeforeMPI, hashedDetAfterMPI);
	  for (int proc=0; proc<mpigetsize(); proc++) 
	    hashedDetBeforeMPI[proc][omp_get_thread_num()].clear();
	  mpi::all_to_all(world, hashedNum1BeforeMPI, hashedNum1AfterMPI);
	  for (int proc=0; proc<mpigetsize(); proc++) 
	    hashedNum1BeforeMPI[proc][omp_get_thread_num()].clear();
	  mpi::all_to_all(world, hashedNum2BeforeMPI, hashedNum2AfterMPI);
	  for (int proc=0; proc<mpigetsize(); proc++) 
	    hashedNum2BeforeMPI[proc][omp_get_thread_num()].clear();
	  mpi::all_to_all(world, hashedEnergyBeforeMPI, hashedEnergyAfterMPI);
	  for (int proc=0; proc<mpigetsize(); proc++) 
	    hashedEnergyBeforeMPI[proc][omp_get_thread_num()].clear();
	  mpi::all_to_all(world, hashedpresentBeforeMPI, hashedpresentAfterMPI);
	  for (int proc=0; proc<mpigetsize(); proc++) 
	    hashedpresentBeforeMPI[proc][omp_get_thread_num()].clear();
	}
#pragma omp barrier


	for (int proc=0; proc<mpigetsize(); proc++) {
	  for (int thrd=0; thrd<num_thrds; thrd++) {
	    for (int j=0; j<hashedDetAfterMPI[proc][thrd][omp_get_thread_num()].size(); j++) {
	      Psi1.push_back(hashedDetAfterMPI[proc][thrd][omp_get_thread_num()].at(j));
	      numerator1A.push_back(hashedNum1AfterMPI[proc][thrd][omp_get_thread_num()].at(j));
	      numerator2A.push_back(hashedNum2AfterMPI[proc][thrd][omp_get_thread_num()].at(j));
	      det_energy.push_back(hashedEnergyAfterMPI[proc][thrd][omp_get_thread_num()].at(j));
	      present.push_back(hashedpresentAfterMPI[proc][thrd][omp_get_thread_num()].at(j));
	    }
	    hashedDetAfterMPI[proc][thrd][omp_get_thread_num()].clear();
	    hashedNum1AfterMPI[proc][thrd][omp_get_thread_num()].clear();
	    hashedNum2AfterMPI[proc][thrd][omp_get_thread_num()].clear();
	    hashedEnergyAfterMPI[proc][thrd][omp_get_thread_num()].clear();
	    hashedpresentAfterMPI[proc][thrd][omp_get_thread_num()].clear();
	  }
	}

	std::vector<Determinant> Psi1copy = Psi1;
	vector<long> detIndex(Psi1.size(), 0);
	vector<long> detIndexcopy(Psi1.size(), 0);
	for (size_t i=0; i<Psi1.size(); i++)
	  detIndex[i] = i;      
	mergesort(&Psi1copy[0], 0, Psi1.size()-1, &detIndex[0], &( Psi1.operator[](0)), &detIndexcopy[0]);
	detIndexcopy.clear(); Psi1copy.clear();      
	reorder(numerator1A, detIndex);
	reorder(numerator2A, detIndex);
	reorder(det_energy, detIndex);
	reorder(present, detIndex);
	detIndex.clear();
      //quickSort( &(Psi1[0]), 0, Psi1.size(), &numerator1A[0], &numerator2A[0], &det_energy, &present);
      }

      CItype currentNum1A=0.; double currentNum2A=0.;
      CItype currentNum1B=0.; double currentNum2B=0.;
      vector<Determinant>::iterator vec_it = SortedDets.begin();
      double energyEN = 0.0, energyENLargeEps = 0.0;      
      size_t effNmc = mpigetsize()*num_thrds*Nmc;
        
    for (int i=0;i<Psi1.size();) {
      if (Psi1[i] < *vec_it) {
	currentNum1A += numerator1A[i];
	currentNum2A += numerator2A[i];
	if (present[i]) {
	  currentNum1B += numerator1A[i];
	  currentNum2B += numerator2A[i];
	}
	
	if ( i == Psi1.size()-1) {
	  energyEN += (pow(abs(currentNum1A),2)*Nmc/(Nmc-1) - currentNum2A)/(det_energy[i] - E0);
	  energyENLargeEps += (pow(abs(currentNum1B),2)*Nmc/(Nmc-1) - currentNum2B)/(det_energy[i] - E0);
	}
	else if (!(Psi1[i] == Psi1[i+1])) {
	  energyEN += ( pow(abs(currentNum1A),2)*Nmc/(Nmc-1) - currentNum2A)/(det_energy[i] - E0);
	  energyENLargeEps += ( pow(abs(currentNum1B),2)*Nmc/(Nmc-1) - currentNum2B)/(det_energy[i] - E0);
	  currentNum1A = 0.;
	  currentNum2A = 0.;
	  currentNum1B = 0.;
	  currentNum2B = 0.;
	}
	i++;
      }
      else if (*vec_it <Psi1[i] && vec_it != SortedDets.end())
	vec_it++;
      else if (*vec_it <Psi1[i] && vec_it == SortedDets.end()) {
	currentNum1A += numerator1A[i];
	currentNum2A += numerator2A[i];
	if (present[i]) {
	  currentNum1B += numerator1A[i];
	  currentNum2B += numerator2A[i];
	}
	
	if ( i == Psi1.size()-1) {
	  energyEN += ( pow(abs(currentNum1A),2)*Nmc/(Nmc-1) - currentNum2A)/(det_energy[i] - E0);
	  energyENLargeEps += ( pow(abs(currentNum1B),2)*Nmc/(Nmc-1) - currentNum2B)/(det_energy[i] - E0);
	}
	else if (!(Psi1[i] == Psi1[i+1])) {
	  energyEN += ( pow(abs(currentNum1A),2)*Nmc/(Nmc-1) - currentNum2A)/(det_energy[i] - E0);
	  energyENLargeEps += ( pow(abs(currentNum1B),2)*Nmc/(Nmc-1) - currentNum2B)/(det_energy[i] - E0);
	  //energyEN += (currentNum1A*currentNum1A*Nmc/(Nmc-1) - currentNum2A)/(det_energy[i] - E0);
	  //energyENLargeEps += (currentNum1B*currentNum1B*Nmc/(Nmc-1) - currentNum2B)/(det_energy[i] - E0);
	  currentNum1A = 0.;
	  currentNum2A = 0.;
	  currentNum1B = 0.;
	  currentNum2B = 0.;
	}
	i++;
      }
      else {
	if (Psi1[i] == Psi1[i+1])
	  i++;
	else {
	  vec_it++; i++;
	}
      }
    }
      
      totalPT=0; totalPTLargeEps=0;
#pragma omp barrier
#pragma omp critical
      {
	totalPT += energyEN;
	totalPTLargeEps += energyENLargeEps;
      }
#pragma omp barrier
      
      double finalE = 0., finalELargeEps=0;
      if(omp_get_thread_num() == 0) mpi::all_reduce(world, totalPT, finalE, std::plus<double>());
      if(omp_get_thread_num() == 0) mpi::all_reduce(world, totalPTLargeEps, finalELargeEps, std::plus<double>());
      
      if (mpigetrank() == 0 && omp_get_thread_num() == 0) {
	AvgenergyEN += -finalE+finalELargeEps+EptLarge; currentIter++;
	std::cout << format("%6i  %14.8f  %s%i %14.8f   %10.2f  %10i") 
	  %(currentIter) % (E0-finalE+finalELargeEps+EptLarge) % ("Root") % root % (E0+AvgenergyEN/currentIter) % (getTime()-startofCalc) % sampleSize ;
	cout << endl;
      }
    }
  }
  
}

void getDeterminantsDeterministicPTInt1(Determinant det, int det_ind, double epsilon1, CItype ci1, double epsilon2, CItype ci2, oneInt& int1a, oneInt& int1, twoInt& int2, vector<int>& irreps, double coreE, std::vector<Determinant>& dets, std::vector<CItype>& numerator1, std::vector<CItype>& numerator2, std::vector<double>& energy, schedule& schd, int nelec) {

  int norbs = det.norbs;
  vector<int> closed(nelec,0);
  vector<int> open(norbs-nelec,0);
  det.getOpenClosed(open, closed);
  int nclosed = nelec;
  int nopen = norbs-nclosed;
  size_t orbDiff;

  for (int ia=0; ia<nopen*nclosed; ia++){
    int i=ia/nopen, a=ia%nopen;

    double sgn = 1.0;
    det.parity(min(open[a],closed[i]), max(open[a],closed[i]),sgn);
    CItype integral = int1a(open[a], closed[i])*sgn;

    if (fabs(integral) > epsilon1 || fabs(integral) > epsilon2 ) {
      dets.push_back(det); Determinant& di = *dets.rbegin();
      di.setocc(open[a], true); di.setocc(closed[i],false);

      double E = di.Energy(int1, int2, coreE);
      energy.push_back(E);


      //if(fabs(integral) >epsilon1)
	numerator1.push_back(integral*ci1);
	//else
	//numerator1.push_back(0.0);

	//if(fabs(integral) > epsilon2)
	numerator2.push_back(integral*ci2);
	//else
	//numerator2.push_back(0.0);
    }
  }
  return;
}

