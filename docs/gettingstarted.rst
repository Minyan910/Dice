Getting Started
***************
Using SHCI as a stand-alone program
-----------------------------------
You can choose to use SHCI as a wavefunction solver without interfacing it with other programs. This will require a two-body integral file in the FCIDUMP format, which can be generated using whatever electronic structure package you prefer. If you'd like to use it as an active space solver in a CASSCF calculation see the :ref:`interfacing-with-pyscf` page.

Once you have generated your FCIDUMP file, you must create an input file that contains all of the parameters that SHCI needs to run a calculation. An example input.dat file is shown below. For more input files see the test directory inside your main SHCI directory.

.. code-block:: none

	nocc 8
	0 1  4 5  8 9  14 15
	end

	sampleN 200
	davidsontol 5.e-5
	dE 1.e-7
	DoRDM

	schedule
	0 9e-4
	3 5e-4
	end

	epsilon2 1.e-8
	deterministic
	noio
	maxiter 12


In the first part of the input file, between lines 1 and 3, we establish the number of electrons in the active space (8 in this case) and then we specify which orbitals each of those eight electrons is in. You can then add keywords either before or after the schedule the order does not matter. For more details about the keywords used or other keywords please see the :ref:`dice-keywords` page. The schedule specifies which :math:`\epsilon_1` values will be used at each iteration. In the example shown, an :math:`\epsilon_1` value of :math:`9*10^{-4}` will be used from the :math:`0^{th}` up to the :math:`3^{rd}` iteration, i.e. in iterations 0, 1, and 2.

Once you have your input.dat and FCIDUMP file you can open a terminal and navigate to the directory with both files and use the following command:

.. code-block:: bash

	mpirun -np 2 /path_to_shci/SHCI input.dat > output.dat


This will execute your input.dat file and write all output to the output.dat file in your current working directory. An example of the output is shown below:

.. code-block:: none

	#nocc 8
	#0 1 4 5 8 9 14 15
	#
	#sampleN 200
	#davidsontol 5.e-5
	#dE 1.e-7
	#DoRDM true
	#
	#schedule
	#0 9e-4
	#3 5e-4
	#end
	#
	#epsilon2 1.e-8
	#deterministic
	#noio
	#maxiter 12
	##nPTiter 0
	#
	#using seed: 1493622999
	#Making Helpers                                        0.00
	#HF = -75.3869032802436
	# 1  1  4
	#BetaN                                                 0.00
	#AlphaN-1                                              0.00
	#-------------Iter=   0---------------
	#Making Helpers                                        0.00
	# 177  57  100
	#BetaN                                                 0.00
	#AlphaN-1                                              0.00
	#niter:  0 root: -1 -> Energy :       -75.38690328
	#niter:  6 root:  0 -> Energy :        -75.4797232
	###########################################            0.01
	#-------------Iter=   1---------------
	#Initial guess(PT) :        -75.4797232
	#Making Helpers                                        0.01
	# 929  168  170
	#BetaN                                                 0.01
	#AlphaN-1                                              0.01
	#niter:  0 root: -1 -> Energy :        -75.4797232
	#niter:  7 root:  0 -> Energy :       -75.48399527
	###########################################            0.01
	#-------------Iter=   2---------------
	#Initial guess(PT) :       -75.48399527
	#Making Helpers                                        0.01
	# 959  168  170
	#BetaN                                                 0.01
	#AlphaN-1                                              0.01
	#niter:  0 root: -1 -> Energy :       -75.48399527
	#niter:  4 root:  0 -> Energy :       -75.48401296
	###########################################            0.02
	#-------------Iter=   3---------------
	#Initial guess(PT) :       -75.48401296
	#Making Helpers                                        0.02
	# 1691  234  198
	#BetaN                                                 0.02
	#AlphaN-1                                              0.02
	#niter:  0 root: -1 -> Energy :       -75.48401296
	#niter:  6 root:  0 -> Energy :       -75.48421962
	###########################################            0.03
	#-------------Iter=   4---------------
	#Initial guess(PT) :       -75.48421962
	#Making Helpers                                        0.03
	# 1705  234  198
	#BetaN                                                 0.03
	#AlphaN-1                                              0.03
	#niter:  0 root: -1 -> Energy :       -75.48421962
	#niter:  3 root:  0 -> Energy :       -75.48422229
	###########################################            0.03
	#-------------Iter=   5---------------
	#Initial guess(PT) :       -75.48422229
	#Making Helpers                                        0.03
	# 1705  234  198
	#BetaN                                                 0.03
	#AlphaN-1                                              0.03
	#niter:  0 root: -1 -> Energy :       -75.48422229
	#niter:  1 root:  0 -> Energy :       -75.48422229
	#Begin writing variational wf                          0.03
	#End   writing variational wf                          0.05
	E from 2RDM: -75.4842222865948
	### IMPORTANT DETERMINANTS FOR STATE: 0
	#0  -0.962972405251402  0.962972405251402  2 0 2 0 2   0 0 2 0 0   0 0
	#1  0.113332103493894  0.113332103493894  2 0 2 0 0   0 0 2 0 0   2 0
	#2  0.113332103493883  0.113332103493883  2 0 0 0 2   0 0 2 0 2   0 0
	#3  0.0778346380808918  0.0778346380808918  2 0 b 0 a   0 0 2 0 a   b 0
	#4  0.0778346380808912  0.0778346380808912  2 0 a 0 b   0 0 2 0 b   a 0
	#5  0.0620766498642539  0.0620766498642539  2 0 2 0 2   0 0 0 0 0   2 0
	### PERFORMING PERTURBATIVE CALCULATION
	# 0
	#Before hash 0.104489803314209
	#After hash 0.110838890075684
	#After all_to_all 0.163533926010132
	#After collecting 0.178984880447388
	#Unique determinants 0.178997993469238
	#Done energy -75.4844111804806  0.180866956710815