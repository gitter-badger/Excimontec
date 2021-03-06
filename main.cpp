// Copyright (c) 2018 Michael C. Heiber
// This source file is part of the Excimontec project, which is subject to the MIT License.
// For more information, see the LICENSE file that accompanies this software.
// The Excimontec project can be found on Github at https://github.com/MikeHeiber/Excimontec

#include "OSC_Sim.h"
#include <mpi.h>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <ctime>
#include <functional>

using namespace std;
using namespace Utils;

struct Parameters_main{
    bool Enable_import_morphology_single;
    string Morphology_filename;
    bool Enable_import_morphology_set;
    string Morphology_set_format;
    int N_test_morphologies;
    int N_morphology_set_size;
	bool Enable_extraction_map_output;
};

//Declare Functions
bool importParameters(ifstream& inputfile,Parameters_main& params_main,Parameters_OPV& params);

int main(int argc, char *argv[]) {
	string version = "v1.0-beta.3";
	// Parameters
	bool End_sim = false;
	// File declaration
	ifstream parameterfile;
	ifstream morphologyfile;
	ofstream logfile;
	ofstream resultsfile;
	ofstream analysisfile;
	stringstream ss;
	// Initialize variables
	string parameterfilename;
	string logfilename;
	Parameters_main params_main;
	Parameters_OPV params_opv;
	int nproc = 1;
	int procid = 0;
	int elapsedtime;
	time_t time_start, time_end;
	bool success;
	bool all_finished = false;
	vector<bool> proc_finished;
	vector<bool> error_status_vec;
	vector<string> error_messages;
	char error_found = (char)0;
	// Start timer
	time_start = time(NULL);
	// Import parameters and options from parameter file and command line arguments
	cout << "Loading input parameters from file... " << endl;
	parameterfilename = argv[1];
	parameterfile.open(parameterfilename.c_str(), ifstream::in);
	if (!parameterfile.good()) {
		cout << "Error loading parameter file.  Program will now exit." << endl;
		return 0;
	}
	success = importParameters(parameterfile, params_main, params_opv);
	parameterfile.close();
	if (!success) {
		cout << "Error importing parameters from parameter file.  Program will now exit." << endl;
		return 0;
	}
	cout << "Parameter loading complete!" << endl;
	// Initialize mpi options
	cout << "Initializing MPI options... ";
	MPI_Init(&argc, &argv);
	MPI_Comm_size(MPI_COMM_WORLD, &nproc);
	MPI_Comm_rank(MPI_COMM_WORLD, &procid);
	cout << procid << ": MPI initialization complete!" << endl;
	// Initialize error monitoring vectors
	proc_finished.assign(nproc, false);
	error_status_vec.assign(nproc, false);
	error_messages.assign(nproc, "");
	// Morphology set import handling
	if (params_main.Enable_import_morphology_set && params_main.N_test_morphologies > nproc) {
		cout << "Error! The number of requested processors cannot be less than the number of morphologies tested." << endl;
		cout << "You have requested " << nproc << " processors for " << params_main.N_test_morphologies << " morphologies." << endl;
		return 0;
	}
	if (params_main.Enable_import_morphology_set) {
		int* selected_morphologies = (int *)malloc(sizeof(int)*nproc);
		if (procid == 0) {
			default_random_engine generator((int)time(0));
			vector<int> morphology_set_original(params_main.N_test_morphologies);
			vector<int> morphology_set;
			// Select morphologies from the morphology set
			for (int n = 0; n < params_main.N_morphology_set_size; n++) {
				morphology_set.push_back(n);
			}
			shuffle(morphology_set.begin(), morphology_set.end(), generator);
			for (int n = 0; n < params_main.N_test_morphologies; n++) {
				morphology_set_original[n] = morphology_set[n];
			}
			// Assign morphologies to each processor
			morphology_set.clear();
			for (int n = 0; n < nproc; n++) {
				// Fill morphology set when empty and shuffle
				if ((int)morphology_set.size() == 0) {
					morphology_set = morphology_set_original;
					shuffle(morphology_set.begin(), morphology_set.end(), generator);
				}
				// Assign morphology from the back of the set and remove it from the set
				selected_morphologies[n] = morphology_set.back();
				morphology_set.pop_back();
			}
		}
		MPI_Barrier(MPI_COMM_WORLD);
		MPI_Bcast(selected_morphologies, nproc, MPI_INT, 0, MPI_COMM_WORLD);
		// Parse input morphology set file format
		int pos = (int)params_main.Morphology_set_format.find("#");
		string prefix = params_main.Morphology_set_format.substr(0, pos);
		string suffix = params_main.Morphology_set_format.substr(pos + 1);
		cout << procid << ": Morphology " << selected_morphologies[procid] << " selected." << endl;
		ss << prefix << selected_morphologies[procid] << suffix;
		cout << procid << ": " << ss.str() << " selected." << endl;
		params_main.Morphology_filename = ss.str();
		ss.str("");
	}
	if (params_main.Enable_import_morphology_single || params_main.Enable_import_morphology_set) {
		params_opv.Enable_import_morphology = true;
		morphologyfile.open(params_main.Morphology_filename.c_str(), ifstream::in);
		if (morphologyfile.good()) {
			params_opv.Morphology_file = &morphologyfile;
		}
		else {
			cout << procid << ": Error opening morphology file for importing." << endl;
			return 0;
		}
	}
	else {
		params_opv.Enable_import_morphology = false;
	}
	// Setup file output
	cout << procid << ": Creating output files..." << endl;
	if (params_opv.Enable_logging) {
		ss << "log" << procid << ".txt";
		logfilename = ss.str();
		logfile.open(ss.str().c_str());
		ss.str("");
	}
	params_opv.Logfile = &logfile;
	// Initialize Simulation
	cout << procid << ": Initializing simulation " << procid << "..." << endl;
	OSC_Sim sim;
	success = sim.init(params_opv, procid);
	if (!success) {
		cout << procid << ": Initialization failed, simulation will now terminate." << endl;
		return 0;
	}
	cout << procid << ": Simulation initialization complete" << endl;
	if (params_opv.Enable_exciton_diffusion_test) {
		cout << procid << ": Starting exciton diffusion test..." << endl;
	}
	else if (params_opv.Enable_dynamics_test) {
		cout << procid << ": Starting dynamics test..." << endl;
	}
	else if (params_opv.Enable_ToF_test) {
		cout << procid << ": Starting time-of-flight charge transport test..." << endl;
	}
	else if (params_opv.Enable_IQE_test) {
		cout << procid << ": Starting internal quantum efficiency test..." << endl;
	}
	// Begin Simulation loop
	// Simulation ends for all procs with procid >0 when End_sim is true
	// Proc 0 only ends when End_sim is true and all_finished is true
	while (!End_sim || (procid == 0 && !all_finished)) {
		if (!End_sim) {
			success = sim.executeNextEvent();
			if (!success) {
				cout << procid << ": Event execution failed, simulation will now terminate." << endl;
			}
			End_sim = sim.checkFinished();
		}
		// Check for errors
		if (!success || sim.getN_events_executed() % 500000 == 0 || End_sim) {
			// Send completion status, error status, message length, and message content to proc 0
			char finished_status;
			char error_status;
			char msg_length;
			for (int i = 1; i < nproc; i++) {
				// Send status messages to proc 0
				if (procid == i) {
					finished_status = End_sim ? (char)1 : (char)0;
					error_status = !success ? (char)1 : (char)0;
					MPI_Send(&error_status, 1, MPI_CHAR, 0, i, MPI_COMM_WORLD);
					// If the proc has an error, send the error message to proc 0
					if (!success) {
						msg_length = (char)sim.getErrorMessage().size();
						MPI_Send(&msg_length, 1, MPI_CHAR, 0, i, MPI_COMM_WORLD);
						MPI_Send(sim.getErrorMessage().c_str(), (int)msg_length, MPI_CHAR, 0, i, MPI_COMM_WORLD);
					}
					MPI_Send(&finished_status, 1, MPI_CHAR, 0, i, MPI_COMM_WORLD);
				}
				// Receive messages from any processors not previously finished
				if (procid == 0 && !proc_finished[i]) {
					// Receive error status message
					MPI_Recv(&error_status, 1, MPI_CHAR, i, i, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
					error_status_vec[i] = (error_status == (char)1) ? true : false;
					// If the proc has an error, then receive the error message
					if (error_status_vec[i]) {
						MPI_Recv(&msg_length, 1, MPI_CHAR, i, i, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
						char* error_msg = (char *)malloc(sizeof(char) * (int)msg_length);
						MPI_Recv(error_msg, (int)msg_length, MPI_CHAR, i, i, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
						error_messages[i] = string(error_msg);
						error_found = (char)1;
					}
					MPI_Recv(&finished_status, 1, MPI_CHAR, i, i, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
					proc_finished[i] = (finished_status == (char)1) ? true : false;
				}
			}
			// Check error status of proc 0
			if (!success) {
				error_found = (char)1;
			}
			// Send error status from proc 0 to all unfinished procs
			for (int i = 1; i < nproc; i++) {
				if (procid == 0) {
					MPI_Send(&error_found, 1, MPI_CHAR, i, i, MPI_COMM_WORLD);
				}
				if (procid == i && !proc_finished[i]) {
					MPI_Recv(&error_found, 1, MPI_CHAR, 0, i, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
				}
			}
			// Update info for proc 0
			error_status_vec[0] = !success;
			proc_finished[0] = End_sim;
			error_messages[0] = sim.getErrorMessage();
			if (error_found == (char)1) {
				break;
			}
			// Update completion status
			if (procid == 0) {
				all_finished = true;
				for (int i = 0; i < nproc; i++) {
					if (!proc_finished[i]) {
						all_finished = false;
						break;
					}
				}
			}
			// Output status
			if (sim.getN_events_executed() % 1000000 == 0) {
				sim.outputStatus();
			}
			// Reset logfile
			if (params_opv.Enable_logging) {
				if (sim.getN_events_executed() % 1000 == 0) {
					logfile.close();
					logfile.open(logfilename.c_str());
				}
			}
		}
	}
	if (params_opv.Enable_logging) {
		logfile.close();
	}
	cout << procid << ": Simulation finished." << endl;
	time_end = time(NULL);
	elapsedtime = (int)difftime(time_end, time_start);
	// Output result summary for each processor
	ss << "results" << procid << ".txt";
	resultsfile.open(ss.str().c_str());
	ss.str("");
	resultsfile << "Excimontec " << version << " Results:\n";
	resultsfile << "Calculation time elapsed is " << (double)elapsedtime / 60 << " minutes.\n";
	resultsfile << sim.getTime() << " seconds have been simulated.\n";
	resultsfile << sim.getN_events_executed() << " events have been executed.\n";
	if (!success) {
		resultsfile << "An error occured during the simulation:" << endl;
		resultsfile << sim.getErrorMessage() << endl;
	}
	else {
		if (params_opv.Enable_exciton_diffusion_test) {
			resultsfile << "Exciton diffusion test results:\n";
			resultsfile << sim.getN_excitons_created() << " excitons have been created.\n";
			resultsfile << "Exciton Diffusion Length is " << sim.calculateDiffusionLength_avg() << " � " << sim.calculateDiffusionLength_stdev() << " nm.\n";
		}
		else if (params_opv.Enable_ToF_test) {
			resultsfile << "Time-of-flight charge transport test results:\n";
			if (!params_opv.ToF_polaron_type) {
				resultsfile << sim.getN_electrons_collected() << " of " << sim.getN_electrons_created() << " electrons have been collected.\n";
			}
			else {
				resultsfile << sim.getN_holes_collected() << " of " << sim.getN_holes_created() << " holes have been collected.\n";
			}
			resultsfile << "Transit time is " << sim.calculateTransitTime_avg() << " � " << sim.calculateTransitTime_stdev() << " s.\n";
			resultsfile << "Charge carrier mobility is " << sim.calculateMobility_avg() << " � " << sim.calculateMobility_stdev() << " cm^2 V^-1 s^-1.\n";
		}
		if (params_opv.Enable_dynamics_test) {
			resultsfile << "Dynamics test results:\n";
			resultsfile << sim.getN_excitons_created() << " initial excitons were created.\n";
		}
		if (params_opv.Enable_IQE_test) {
			resultsfile << "Internal quantum efficiency test results:\n";
			resultsfile << sim.getN_excitons_created() << " excitons have been created.\n";
		}
		if (params_opv.Enable_IQE_test || params_opv.Enable_dynamics_test) {
			resultsfile << sim.getN_excitons_created((short)1) << " excitons were created on donor sites.\n";
			resultsfile << sim.getN_excitons_created((short)2) << " excitons were created on acceptor sites.\n";
			resultsfile << 100 * (double)sim.getN_excitons_dissociated() / (double)sim.getN_excitons_created() << "% of excitons have dissociated.\n";
			resultsfile << 100 * (double)sim.getN_singlet_excitons_recombined() / (double)sim.getN_excitons_created() << "% of excitons relaxed to the ground state as singlets.\n";
			resultsfile << 100 * (double)sim.getN_triplet_excitons_recombined() / (double)sim.getN_excitons_created() << "% of excitons relaxed to the ground state as triplets.\n";
			resultsfile << 100 * (double)sim.getN_singlet_singlet_annihilations() / (double)sim.getN_excitons_created() << "% of excitons were lost to singlet-singlet annihilation.\n";
			resultsfile << 100 * (double)sim.getN_singlet_triplet_annihilations() / (double)sim.getN_excitons_created() << "% of excitons were lost to singlet-triplet annihilation.\n";
			resultsfile << 100 * (double)sim.getN_triplet_triplet_annihilations() / (double)sim.getN_excitons_created() << "% of excitons were lost to triplet-triplet annihilation.\n";
			resultsfile << 100 * (double)sim.getN_singlet_polaron_annihilations() / (double)sim.getN_excitons_created() << "% of excitons were lost to singlet-polaron annihilation.\n";
			resultsfile << 100 * (double)sim.getN_triplet_polaron_annihilations() / (double)sim.getN_excitons_created() << "% of excitons were lost to triplet-polaron annihilation.\n";
			resultsfile << 100 * (double)sim.getN_geminate_recombinations() / (double)sim.getN_excitons_dissociated() << "% of photogenerated charges were lost to geminate recombination.\n";
			resultsfile << 100 * (double)sim.getN_bimolecular_recombinations() / (double)sim.getN_excitons_dissociated() << "% of photogenerated charges were lost to bimolecular recombination.\n";
			resultsfile << 100 * (double)(sim.getN_electrons_collected() + sim.getN_holes_collected()) / (2 * (double)sim.getN_excitons_dissociated()) << "% of photogenerated charges were extracted.\n";
		}
		if (params_opv.Enable_IQE_test) {
			resultsfile << "IQE = " << 100 * (double)(sim.getN_electrons_collected() + sim.getN_holes_collected()) / (2 * (double)sim.getN_excitons_created()) << "% with an internal potential of " << params_opv.Internal_potential << " V." << endl;
		}
		resultsfile << endl;
	}
	resultsfile.close();
	// Output charge extraction map data
	if (success && params_main.Enable_extraction_map_output && (params_opv.Enable_ToF_test || params_opv.Enable_IQE_test)) {
		if (params_opv.Enable_ToF_test) {
			ss << "Charge_extraction_map" << procid << ".txt";
			string filename = ss.str();
			ss.str("");
			vector<string> extraction_data = sim.getChargeExtractionMap(params_opv.ToF_polaron_type);
			outputVectorToFile(extraction_data, filename);
		}
		if (params_opv.Enable_IQE_test) {
			ss << "Electron_extraction_map" << procid << ".txt";
			string filename = ss.str();
			ss.str("");
			vector<string> extraction_data = sim.getChargeExtractionMap(false);
			outputVectorToFile(extraction_data, filename);
			ss << "Hole_extraction_map" << procid << ".txt";
			filename = ss.str();
			ss.str("");
			extraction_data = sim.getChargeExtractionMap(true);
			outputVectorToFile(extraction_data, filename);
		}
	}
	// Output overall analysis results from all processors
	int elapsedtime_sum;
	MPI_Reduce(&elapsedtime, &elapsedtime_sum, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
	if (procid == 0) {
		ss << "analysis_summary.txt";
		analysisfile.open(ss.str().c_str());
		ss.str("");
		analysisfile << "Excimontec " << version << " Results Summary:\n";
		analysisfile << "Simulation was performed on " << nproc << " processors.\n";
		analysisfile << "Average calculation time was " << (double)elapsedtime_sum / (60 * nproc) << " minutes.\n";
		if (error_found == (char)1) {
			analysisfile << endl << "An error occured on one or more processors:" << endl;
			for (int i = 0; i < nproc; i++) {
				if (error_status_vec[i]) {
					analysisfile << i << ": " << error_messages[i] << endl;
				}
			}
		}
	}
	if (error_found == (char)0 && params_opv.Enable_exciton_diffusion_test) {
		vector<double> diffusion_data;
		diffusion_data = MPI_gatherVectors(sim.getDiffusionData());
		if (procid == 0) {
			analysisfile << "Overall exciton diffusion test results:\n";
			analysisfile << nproc*(sim.getN_singlet_excitons_recombined() + sim.getN_triplet_excitons_recombined()) << " total excitons tested." << endl;
			analysisfile << "Exciton diffusion length is " << vector_avg(diffusion_data) << " � " << vector_stdev(diffusion_data) << " nm.\n";
		}
	}
	if (error_found == (char)0 && params_opv.Enable_ToF_test) {
		int N_transient_cycles = sim.getN_transient_cycles();
		int N_transient_cycles_sum;
		MPI_Reduce(&N_transient_cycles, &N_transient_cycles_sum, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
		vector<double> transit_times = MPI_gatherVectors(sim.getTransitTimeData());
		int transit_attempts = ((sim.getN_electrons_collected() > sim.getN_holes_collected()) ? sim.getN_electrons_created() : (sim.getN_holes_created()));
		int transit_attempts_total;
		MPI_Reduce(&transit_attempts, &transit_attempts_total, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
		vector<int> counts = MPI_calculateVectorSum(sim.getToFTransientCounts());
		vector<double> energies = MPI_calculateVectorSum(sim.getToFTransientEnergies());
		vector<double> velocities = MPI_calculateVectorSum(sim.getToFTransientVelocities());
		vector<double> times = sim.getToFTransientTimes();
		if (procid == 0) {
			// ToF main results output
			vector<double> mobilities = sim.calculateMobilities(transit_times);
			double electric_field = fabs(sim.getInternalField());
			ofstream tof_resultsfile;
			ss << "ToF_results.txt";
			tof_resultsfile.open(ss.str().c_str());
			ss.str("");
			tof_resultsfile << "Electric Field (V/cm),Transit Time Avg (s),Transit Time Stdev (s),Mobility Avg (cm^2 V^-1 s^-1),Mobility Stdev (cm^2 V^-1 s^-1)" << endl;
			tof_resultsfile << electric_field << "," << vector_avg(transit_times) << "," << vector_stdev(transit_times) << "," << vector_avg(mobilities) << "," << vector_stdev(mobilities) << endl;
			tof_resultsfile.close();
			// ToF transient output
			ofstream transientfile;
			ss << "ToF_average_transients.txt";
			transientfile.open(ss.str().c_str());
			ss.str("");
			transientfile << "Time (s),Current (mA cm^-2),Average Mobility (cm^2 V^-1 s^-1),Average Energy (eV),Carrier Density (cm^-3)" << endl;
			double volume_total = N_transient_cycles_sum*sim.getVolume();
			for (int i = 0; i < (int)velocities.size(); i++) {
				if (counts[i] > 0 && counts[i] > 5 * N_transient_cycles_sum) {
					transientfile << times[i] << "," << 1000.0 * Elementary_charge*velocities[i] / volume_total << "," << (velocities[i] / (double)counts[i]) / electric_field << "," << energies[i] / (double)counts[i] << "," << (double)counts[i] / volume_total << endl;
				}
				else if (counts[i] > 0) {
					transientfile << times[i] << "," << 1000.0 * Elementary_charge*velocities[i] / volume_total << "," << (velocities[i] / (double)counts[i]) / electric_field << "," << "NaN" << "," << (double)counts[i] / volume_total << endl;
				}
				else {
					transientfile << times[i] << ",0,NaN,NaN,0" << endl;
				}
			}
			transientfile.close();
			// ToF transit time distribution output
			ofstream transitdistfile;
			ss << "ToF_transit_time_dist.txt";
			transitdistfile.open(ss.str().c_str());
			ss.str("");
			vector<double> transit_dist = sim.calculateTransitTimeDist(transit_times, transit_attempts_total);
			transitdistfile << "Transit Time (s),Probability" << endl;
			for (int i = 0; i < (int)transit_dist.size(); i++) {
				transitdistfile << times[i] << "," << transit_dist[i] << endl;
			}
			transitdistfile.close();
			// Analysis Output
			if (!params_opv.ToF_polaron_type) {
				analysisfile << nproc*sim.getN_electrons_collected() << " total electrons collected out of " << transit_attempts_total << " total attempts.\n";
			}
			else {
				analysisfile << nproc*sim.getN_holes_collected() << " total holes collected out of " << transit_attempts_total << " total attempts.\n";
			}
			analysisfile << "Overall time-of-flight charge transport test results:\n";
			analysisfile << "Transit time is " << vector_avg(transit_times) << " � " << vector_stdev(transit_times) << " s.\n";
			analysisfile << "Charge carrier mobility is " << vector_avg(mobilities) << " � " << vector_stdev(mobilities) << " cm^2 V^-1 s^-1.\n";
		}
	}
	if (error_found == (char)0 && params_opv.Enable_dynamics_test) {
		int N_transient_cycles = sim.getN_transient_cycles();
		int N_transient_cycles_sum;
		MPI_Reduce(&N_transient_cycles, &N_transient_cycles_sum, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
		vector<double> times = sim.getDynamicsTransientTimes();
		vector<int> singlets_total = MPI_calculateVectorSum(sim.getDynamicsTransientSinglets());
		vector<int> triplets_total = MPI_calculateVectorSum(sim.getDynamicsTransientTriplets());
		vector<int> electrons_total = MPI_calculateVectorSum(sim.getDynamicsTransientElectrons());
		vector<int> holes_total = MPI_calculateVectorSum(sim.getDynamicsTransientHoles());
		vector<double> exciton_energies = MPI_calculateVectorSum(sim.getDynamicsExcitonEnergies());
		vector<double> electron_energies = MPI_calculateVectorSum(sim.getDynamicsElectronEnergies());
		vector<double> hole_energies = MPI_calculateVectorSum(sim.getDynamicsHoleEnergies());
		vector<double> exciton_msdv = MPI_calculateVectorSum(sim.getDynamicsExcitonMSDV());
		vector<double> electron_msdv = MPI_calculateVectorSum(sim.getDynamicsElectronMSDV());
		vector<double> hole_msdv = MPI_calculateVectorSum(sim.getDynamicsHoleMSDV());
		if (procid == 0) {
			ofstream transientfile;
			ss << "dynamics_average_transients.txt";
			transientfile.open(ss.str().c_str());
			ss.str("");
			transientfile << "Time (s),Singlet Exciton Density (cm^-3),Triplet Exciton Density (cm^-3),Electron Density (cm^-3),Hole Density (cm^-3)";
			transientfile << ",Average Exciton Energy (eV),Exciton MSDV (cm^2 s^-1)";
			transientfile << ",Average Electron Energy (eV),Electron MSDV (cm^2 s^-1)";
			transientfile << ",Average Hole Energy (eV),Hole MSDV (cm^2 s^-1)" << endl;
			double volume_total = N_transient_cycles_sum*sim.getVolume();
			for (int i = 0; i < (int)times.size(); i++) {
				transientfile << times[i] << "," << singlets_total[i] / volume_total << "," << triplets_total[i] / volume_total << "," << electrons_total[i] / volume_total << "," << holes_total[i] / volume_total;
				if ((singlets_total[i] + triplets_total[i]) > 0 && (singlets_total[i] + triplets_total[i]) > 5 * N_transient_cycles_sum) {
					transientfile << "," << exciton_energies[i] / (singlets_total[i] + triplets_total[i]) << "," << exciton_msdv[i] / (singlets_total[i] + triplets_total[i]);
				}
				else if ((singlets_total[i] + triplets_total[i]) > 0 ) {
					transientfile << "," << "NaN" << "," << exciton_msdv[i] / (singlets_total[i] + triplets_total[i]);
				}
				else {
					transientfile << ",NaN,NaN";
				}
				if (electrons_total[i] > 0 && electrons_total[i] > 5 * N_transient_cycles_sum) {
					transientfile << "," << electron_energies[i] / electrons_total[i] << "," << electron_msdv[i] / electrons_total[i];
				}
				else if (electrons_total[i] > 0) {
					transientfile << "," << "NaN" << "," << electron_msdv[i] / electrons_total[i];
				}
				else {
					transientfile << ",NaN,NaN";
				}
				if (holes_total[i] > 0 && holes_total[i] > 5 * N_transient_cycles_sum) {
					transientfile << "," << hole_energies[i] / holes_total[i] << "," << hole_msdv[i] / holes_total[i] << endl;
				}
				else if (holes_total[i] > 0) {
					transientfile << "," << "NaN" << "," << hole_msdv[i] / holes_total[i] << endl;
				}
				else {
					transientfile << ",NaN,NaN" << endl;
					//transientfile << ",NaN" << endl;
				}
			}
			transientfile.close();
		}
	}
	if (error_found == (char)0 && (params_opv.Enable_dynamics_test || params_opv.Enable_IQE_test)) {
		int excitons_created = sim.getN_excitons_created();
		int excitons_created_total;
		MPI_Reduce(&excitons_created, &excitons_created_total, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
		int excitons_created_donor = sim.getN_excitons_created((short)1);
		int excitons_created_donor_total;
		MPI_Reduce(&excitons_created_donor, &excitons_created_donor_total, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
		int excitons_created_acceptor = sim.getN_excitons_created((short)2);
		int excitons_created_acceptor_total;
		MPI_Reduce(&excitons_created_acceptor, &excitons_created_acceptor_total, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
		int excitons_dissociated = sim.getN_excitons_dissociated();
		int excitons_dissociated_total;
		MPI_Reduce(&excitons_dissociated, &excitons_dissociated_total, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
		int singlet_excitons_recombined = sim.getN_singlet_excitons_recombined();
		int singlet_excitons_recombined_total;
		MPI_Reduce(&singlet_excitons_recombined, &singlet_excitons_recombined_total, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
		int triplet_excitons_recombined = sim.getN_triplet_excitons_recombined();
		int triplet_excitons_recombined_total;
		MPI_Reduce(&triplet_excitons_recombined, &triplet_excitons_recombined_total, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
		int singlet_singlet_annihilations = sim.getN_singlet_singlet_annihilations();
		int singlet_singlet_annihilations_total;
		MPI_Reduce(&singlet_singlet_annihilations, &singlet_singlet_annihilations_total, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
		int singlet_triplet_annihilations = sim.getN_singlet_triplet_annihilations();
		int singlet_triplet_annihilations_total;
		MPI_Reduce(&singlet_triplet_annihilations, &singlet_triplet_annihilations_total, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
		int triplet_triplet_annihilations = sim.getN_triplet_triplet_annihilations();
		int triplet_triplet_annihilations_total;
		MPI_Reduce(&triplet_triplet_annihilations, &triplet_triplet_annihilations_total, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
		int singlet_polaron_annihilations = sim.getN_singlet_polaron_annihilations();
		int singlet_polaron_annihilations_total;
		MPI_Reduce(&singlet_polaron_annihilations, &singlet_polaron_annihilations_total, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
		int triplet_polaron_annihilations = sim.getN_triplet_polaron_annihilations();
		int triplet_polaron_annihilations_total;
		MPI_Reduce(&triplet_polaron_annihilations, &triplet_polaron_annihilations_total, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
		int geminate_recombinations = sim.getN_geminate_recombinations();
		int geminate_recombinations_total;
		MPI_Reduce(&geminate_recombinations, &geminate_recombinations_total, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
		int bimolecular_recombinations = sim.getN_bimolecular_recombinations();
		int bimolecular_recombinations_total;
		MPI_Reduce(&bimolecular_recombinations, &bimolecular_recombinations_total, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
		int electrons_collected = sim.getN_electrons_collected();
		int electrons_collected_total;
		MPI_Reduce(&electrons_collected, &electrons_collected_total, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
		int holes_collected = sim.getN_holes_collected();
		int holes_collected_total;
		MPI_Reduce(&holes_collected, &holes_collected_total, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
		if (procid == 0 && params_opv.Enable_dynamics_test) {
			analysisfile << "Overall dynamics test results:\n";
		}
		if (procid == 0 && params_opv.Enable_IQE_test) {
			analysisfile << "Overall internal quantum efficiency test results:\n";
		}
		if (procid == 0) {
			analysisfile << excitons_created_total << " total excitons have been created.\n";
			analysisfile << excitons_created_donor_total << " excitons were created on donor sites.\n";
			analysisfile << excitons_created_acceptor_total << " excitons were created on acceptor sites.\n";
			analysisfile << 100 * (double)excitons_dissociated_total / (double)excitons_created_total << "% of total excitons have dissociated.\n";
			analysisfile << 100 * (double)singlet_excitons_recombined_total / (double)excitons_created_total << "% of total excitons relaxed to the ground state as singlets.\n";
			analysisfile << 100 * (double)triplet_excitons_recombined_total / (double)excitons_created_total << "% of total excitons relaxed to the ground state as triplets.\n";
			analysisfile << 100 * (double)singlet_singlet_annihilations_total / (double)excitons_created_total << "% of total excitons were lost to singlet-singlet annihilation.\n";
			analysisfile << 100 * (double)singlet_triplet_annihilations_total / (double)excitons_created_total << "% of total excitons were lost to singlet-triplet annihilation.\n";
			analysisfile << 100 * (double)triplet_triplet_annihilations_total / (double)excitons_created_total << "% of total excitons were lost to triplet-triplet annihilation.\n";
			analysisfile << 100 * (double)singlet_polaron_annihilations_total / (double)excitons_created_total << "% of total excitons were lost to singlet-polaron annihilation.\n";
			analysisfile << 100 * (double)triplet_polaron_annihilations_total / (double)excitons_created_total << "% of total excitons were lost to triplet-polaron annihilation.\n";
			analysisfile << 100 * (double)geminate_recombinations_total / (double)excitons_dissociated_total << "% of total photogenerated charges were lost to geminate recombination.\n";
			analysisfile << 100 * (double)bimolecular_recombinations_total / (double)excitons_dissociated_total << "% of total photogenerated charges were lost to bimolecular recombination.\n";
			analysisfile << 100 * (double)(electrons_collected_total + holes_collected_total) / (2 * (double)excitons_dissociated_total) << "% of total photogenerated charges were extracted.\n";
		}
		if (procid == 0 && params_opv.Enable_IQE_test) {
			analysisfile << "IQE = " << 100 * (double)(electrons_collected_total + holes_collected_total) / (2 * (double)excitons_created_total) << "% with an internal potential of " << params_opv.Internal_potential << " V." << endl;
		}
	}
	if (procid == 0) {
		analysisfile.close();
	}
	MPI_Barrier(MPI_COMM_WORLD);
	MPI_Finalize();
	return 0;
}

bool importParameters(ifstream& inputfile,Parameters_main& params_main,Parameters_OPV& params){
    string line;
    string var;
    size_t pos;
    vector<string> stringvars;
    bool error_status = false;
    while(inputfile.good()){
        getline(inputfile,line);
        if((line.substr(0,2)).compare("--")!=0 && (line.substr(0,2)).compare("##")!=0){
            pos = line.find("/",0);
            var = line.substr(0,pos-1);
            stringvars.push_back(var);
        }
    }
    int i = 0;
    // Simulation Parameters
	params.Enable_logging = false;
	// KMC Algorithm Parameters
	params.Enable_FRM = importBooleanParam(stringvars[i], error_status);
	if (error_status) {
		cout << "Error setting first reaction method option." << endl;
		return false;
	}
	i++;
	params.Enable_selective_recalc = importBooleanParam(stringvars[i], error_status);
	if (error_status) {
		cout << "Error setting selective recalculation method option." << endl;
		return false;
	}
	i++;
	params.Recalc_cutoff = atoi(stringvars[i].c_str());
	i++;
	params.Enable_full_recalc = importBooleanParam(stringvars[i], error_status);
	if (error_status) {
		cout << "Error setting full recalculation method option." << endl;
		return false;
	}
	i++;
    //enable_periodic_x
    params.Enable_periodic_x = importBooleanParam(stringvars[i],error_status);
    if(error_status){
        cout << "Error setting x-periodic boundary options" << endl;
        return false;
    }
    i++;
    //enable_periodic_y
    params.Enable_periodic_y = importBooleanParam(stringvars[i],error_status);
    if(error_status){
        cout << "Error setting y-periodic boundary options" << endl;
        return false;
    }
    i++;
    //enable_periodic_z
    params.Enable_periodic_z = importBooleanParam(stringvars[i],error_status);
    if(error_status){
        cout << "Error setting z-periodic boundary options" << endl;
        return false;
    }
    i++;
    params.Length = atoi(stringvars[i].c_str());
    i++;
    params.Width = atoi(stringvars[i].c_str());
    i++;
    params.Height = atoi(stringvars[i].c_str());
    i++;
    params.Unit_size = atof(stringvars[i].c_str());
    i++;
    params.Temperature = atoi(stringvars[i].c_str());
    i++;
	params.Internal_potential = atof(stringvars[i].c_str());
	i++;
    // Film Architecture Parameters
    params.Enable_neat = importBooleanParam(stringvars[i],error_status);
    if(error_status){
        cout << "Error enabling neat film architecture." << endl;
        return false;
    }
    i++;
    params.Enable_bilayer = importBooleanParam(stringvars[i],error_status);
    if(error_status){
        cout << "Error enabling bilayer film architecture." << endl;
        return false;
    }
    i++;
    params.Thickness_donor = atoi(stringvars[i].c_str());
    i++;
    params.Thickness_acceptor = atoi(stringvars[i].c_str());
    i++;
    params.Enable_random_blend = importBooleanParam(stringvars[i],error_status);
    if(error_status){
        cout << "Error enabling random blend film architecture." << endl;
        return false;
    }
    i++;
    params.Acceptor_conc = atof(stringvars[i].c_str());;
    i++;
    params_main.Enable_import_morphology_single = importBooleanParam(stringvars[i],error_status);
    if(error_status){
        cout << "Error enabling morphology import." << endl;
        return false;
    }
    i++;
    params_main.Morphology_filename = stringvars[i];
    i++;
    params_main.Enable_import_morphology_set = importBooleanParam(stringvars[i],error_status);
    if(error_status){
        cout << "Error enabling morphology set import." << endl;
        return false;
    }
    i++;
    params_main.Morphology_set_format = stringvars[i];
    i++;
    params_main.N_test_morphologies = atoi(stringvars[i].c_str());
    i++;
    params_main.N_morphology_set_size = atoi(stringvars[i].c_str());
    i++;
    // Test Parameters
    params.N_tests = atoi(stringvars[i].c_str());
    i++;
    params.Enable_exciton_diffusion_test = importBooleanParam(stringvars[i],error_status);
    if(error_status){
        cout << "Error enabling the exciton diffusion test." << endl;
        return false;
    }
    i++;
    params.Enable_ToF_test = importBooleanParam(stringvars[i],error_status);
    if(error_status){
        cout << "Error enabling the time-of-flight polaron transport test." << endl;
        return false;
    }
    i++;
    if(stringvars[i].compare("electron")==0){
        params.ToF_polaron_type = false;
    }
    else if(stringvars[i].compare("hole")==0){
        params.ToF_polaron_type = true;
    }
    else{
        cout << "Error setting polaron type for the time-of-flight test." << endl;
        return false;
    }
    i++;
    params.ToF_initial_polarons = atoi(stringvars[i].c_str());
    i++;
    params.ToF_transient_start = atof(stringvars[i].c_str());
    i++;
    params.ToF_transient_end = atof(stringvars[i].c_str());
    i++;
    params.ToF_pnts_per_decade = atoi(stringvars[i].c_str());
    i++;
    params.Enable_IQE_test = importBooleanParam(stringvars[i],error_status);
    if(error_status){
        cout << "Error enabling the internal quantum efficiency test." << endl;
        return false;
    }
    i++;
    params.IQE_time_cutoff = atof(stringvars[i].c_str());
    i++;
	params_main.Enable_extraction_map_output = importBooleanParam(stringvars[i], error_status);
	if (error_status) {
		cout << "Error setting charge extraction map output settings." << endl;
		return false;
	}
	i++;
    params.Enable_dynamics_test = importBooleanParam(stringvars[i],error_status);
    if(error_status){
        cout << "Error enabling the dynamics test." << endl;
        return false;
    }
    i++;
    params.Enable_dynamics_extraction = importBooleanParam(stringvars[i],error_status);
    if(error_status){
        cout << "Error setting dynamics test extraction option." << endl;
        return false;
    }
    i++;
    params.Dynamics_initial_exciton_conc = atof(stringvars[i].c_str());
    i++;
    params.Dynamics_transient_start = atof(stringvars[i].c_str());
    i++;
    params.Dynamics_transient_end = atof(stringvars[i].c_str());
    i++;
    params.Dynamics_pnts_per_decade = atoi(stringvars[i].c_str());
    i++;
    // Exciton Parameters
    params.Exciton_generation_rate_donor = atof(stringvars[i].c_str());
    i++;
    params.Exciton_generation_rate_acceptor = atof(stringvars[i].c_str());
    i++;
    params.Singlet_lifetime_donor = atof(stringvars[i].c_str());
    i++;
    params.Singlet_lifetime_acceptor = atof(stringvars[i].c_str());
    i++;
	params.Triplet_lifetime_donor = atof(stringvars[i].c_str());
	i++;
	params.Triplet_lifetime_acceptor = atof(stringvars[i].c_str());
	i++;
    params.R_singlet_hopping_donor = atof(stringvars[i].c_str());
    i++;
    params.R_singlet_hopping_acceptor = atof(stringvars[i].c_str());
    i++;
	params.Singlet_localization_donor = atof(stringvars[i].c_str());
	i++;
	params.Singlet_localization_acceptor = atof(stringvars[i].c_str());
	i++;
	params.R_triplet_hopping_donor = atof(stringvars[i].c_str());
	i++;
	params.R_triplet_hopping_acceptor = atof(stringvars[i].c_str());
	i++;
	params.Triplet_localization_donor = atof(stringvars[i].c_str());
	i++;
	params.Triplet_localization_acceptor = atof(stringvars[i].c_str());
	i++;
	params.Enable_FRET_triplet_annihilation = importBooleanParam(stringvars[i], error_status);
	if (error_status) {
		cout << "Error setting FRET triplet annihilation option." << endl;
		return false;
	}
	i++;
	params.R_exciton_exciton_annihilation_donor = atof(stringvars[i].c_str());
	i++;
	params.R_exciton_exciton_annihilation_acceptor = atof(stringvars[i].c_str());
	i++;
	params.R_exciton_polaron_annihilation_donor = atof(stringvars[i].c_str());
	i++;
	params.R_exciton_polaron_annihilation_acceptor = atof(stringvars[i].c_str());
	i++;
    params.FRET_cutoff = atoi(stringvars[i].c_str());
    i++;
    params.E_exciton_binding_donor = atof(stringvars[i].c_str());
    i++;
    params.E_exciton_binding_acceptor = atof(stringvars[i].c_str());
    i++;
    params.R_exciton_dissociation_donor = atof(stringvars[i].c_str());
    i++;
    params.R_exciton_dissociation_acceptor = atof(stringvars[i].c_str());
    i++;
    params.Exciton_dissociation_cutoff = atoi(stringvars[i].c_str());
    i++;
	params.R_exciton_isc_donor = atof(stringvars[i].c_str());
	i++;
	params.R_exciton_isc_acceptor = atof(stringvars[i].c_str());
	i++;
	params.R_exciton_risc_donor = atof(stringvars[i].c_str());
	i++;
	params.R_exciton_risc_acceptor = atof(stringvars[i].c_str());
	i++;
	params.E_exciton_ST_donor = atof(stringvars[i].c_str());
	i++;
	params.E_exciton_ST_acceptor = atof(stringvars[i].c_str());
	i++;
    // Polaron Parameters
    params.Enable_phase_restriction = importBooleanParam(stringvars[i],error_status);
    if(error_status){
        cout << "Error setting polaron phase restriction option." << endl;
        return false;
    }
    i++;
    params.R_polaron_hopping_donor = atof(stringvars[i].c_str());
    i++;
    params.R_polaron_hopping_acceptor = atof(stringvars[i].c_str());
    i++;
    params.Polaron_localization_donor = atof(stringvars[i].c_str());
    i++;
    params.Polaron_localization_acceptor = atof(stringvars[i].c_str());
    i++;
    params.Enable_miller_abrahams = importBooleanParam(stringvars[i],error_status);
    if(error_status){
        cout << "Error setting Miller-Abrahams polaron hopping model options" << endl;
        return false;
    }
    i++;
    params.Enable_marcus = importBooleanParam(stringvars[i],error_status);
    if(error_status){
        cout << "Error setting Marcus polaron hopping model options" << endl;
        return false;
    }
    i++;
    params.Reorganization_donor = atof(stringvars[i].c_str());
    i++;
    params.Reorganization_acceptor = atof(stringvars[i].c_str());
    i++;
    params.R_polaron_recombination = atof(stringvars[i].c_str());
    i++;
    params.Polaron_hopping_cutoff = atoi(stringvars[i].c_str());
    i++;
    params.Enable_gaussian_polaron_delocalization = importBooleanParam(stringvars[i],error_status);
    if(error_status){
        cout << "Error setting Gaussian polaron delocalization option." << endl;
        return false;
    }
    i++;
    params.Polaron_delocalization_length = atof(stringvars[i].c_str());
    i++;
    // Lattice Parameters
    params.Homo_donor = atof(stringvars[i].c_str());
    i++;
    params.Lumo_donor = atof(stringvars[i].c_str());
    i++;
    params.Homo_acceptor = atof(stringvars[i].c_str());
    i++;
    params.Lumo_acceptor = atof(stringvars[i].c_str());
    i++;
    //enable_gaussian_dos
    params.Enable_gaussian_dos = importBooleanParam(stringvars[i],error_status);
    if(error_status){
        cout << "Error setting Gaussian DOS options" << endl;
        return false;
    }
    i++;
    params.Energy_stdev_donor = atof(stringvars[i].c_str());
    i++;
    params.Energy_stdev_acceptor = atof(stringvars[i].c_str());
    i++;
    //enable_exponential_dos
    params.Enable_exponential_dos = importBooleanParam(stringvars[i],error_status);
    if(error_status){
        cout << "Error setting Exponential DOS options" << endl;
        return false;
    }
    i++;
    params.Energy_urbach_donor = atof(stringvars[i].c_str());
    i++;
    params.Energy_urbach_acceptor = atof(stringvars[i].c_str());
    i++;
	//enable_correlated_disorder
	params.Enable_correlated_disorder = importBooleanParam(stringvars[i], error_status);
	if (error_status) {
		cout << "Error setting Correlated Disorder options" << endl;
		return false;
	}
	i++;
	params.Disorder_correlation_length = atof(stringvars[i].c_str());
	i++;
	//enable_gaussian_kernel
	params.Enable_gaussian_kernel = importBooleanParam(stringvars[i], error_status);
	if (error_status) {
		cout << "Error setting Correlated Disorder gaussian kernel options" << endl;
		return false;
	}
	i++;
	//enable_power_kernel
	params.Enable_power_kernel = importBooleanParam(stringvars[i], error_status);
	if (error_status) {
		cout << "Error setting Correlated Disorder gaussian kernel options" << endl;
		return false;
	}
	i++;
	params.Power_kernel_exponent = atoi(stringvars[i].c_str());
	i++;
    // Coulomb Calculation Parameters
    params.Dielectric_donor = atof(stringvars[i].c_str());
    i++;
    params.Dielectric_acceptor = atof(stringvars[i].c_str());
    i++;
    params.Coulomb_cutoff = atoi(stringvars[i].c_str());
    i++;
    return true;
}


