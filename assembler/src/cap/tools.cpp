//***************************************************************************
//* Copyright (c) 2011-2013 Saint-Petersburg Academic University
//* All Rights Reserved
//* See file LICENSE for details.
//****************************************************************************

#include "compare_standard.hpp"
#include "logger/log_writers.hpp"
#include "graphio.hpp"
#include <boost/test/unit_test.hpp>

#include "comparison_utils.hpp"
#include "diff_masking.hpp"
#include "repeat_masking.hpp"
#include "genome_correction.hpp"
#include "assembly_compare.hpp"
#include "test_utils.hpp"
#include "gene_analysis.hpp"

namespace cap {

BOOST_AUTO_TEST_CASE( TwoAssemblyComparison ) {
	return;
	utils::TmpFolderFixture _("tmp");
	static const size_t k = 19;
	static const size_t K = 55;
//	static const size_t K = 57;
//	static const size_t K = 53;

//	io::Reader stream_1("/home/snurk/gingi/2.fasta");
//	io::Reader stream_2("/home/snurk/gingi/3.fasta");

	io::Reader stream_1("/home/snurk/Dropbox/lab/phep_issue/phep_master.fasta");
	io::Reader stream_2(
			"/home/snurk/Dropbox/lab/phep_issue/phep_param_opt.fasta");
	string ref =
			"/home/snurk/Dropbox/lab/phep_issue/reference_NC_013061.1.fasta";
	string folder = "phep_issue/results/";
	make_dir(folder);

	RunBPComparison < k, K, runtime_k::RtSeq
			> (stream_1, stream_2, "master_", "opt_", true/*refine*/, false/*untangle*/, folder, true/*detailed_output*/, 5/*delta*/, ReadGenome(
					ref));
}

BOOST_AUTO_TEST_CASE( CompareEcoli ) {
	utils::TmpFolderFixture _("tmp");

	std::string base_path = "/home/snurk/ecoli_refs/";

	vector<std::string> paths = {
			"H6.fasta",
			"K12.fasta"
	};

	vector<size_t> k_sequence = { 5001, 1001, 501, 201, 101, 55, 21 };

//	std::string files_md5 = utils::GenMD5FromFiles(paths);
//	INFO("result is stored with md5 of " << files_md5);

	PerformIterativeRefinement(base_path, paths,
			//"bp_graph_" + files_md5 +
            base_path + "H6_K12_processed/", k_sequence);
}

//BOOST_AUTO_TEST_CASE( MaskDiffsForMultiple ) {
//    return;
//	utils::TmpFolderFixture _("tmp");
//
//	std::string base_path = "/home/snurk/Dropbox/olga_gelf/";
//
//	/*
//	 vector<std::string> paths = {
//	 "/home/valich/mrsa/more_strains/MSSA476.fasta",
//	 "/home/valich/mrsa/more_strains/MRSA252.fasta",
//	 "/home/valich/mrsa/more_strains/TW20.fasta",
//	 "/home/valich/mrsa/more_strains/USA300.fasta"
//	 };
//	 vector<std::string> suffixes = {
//	 "mssa476",
//	 "rmsa252",
//	 "tw20",
//	 "usa300"
//	 };
//
//	 base_path + "CCDC5079.fasta",
//	 base_path + "CCDC5180.fasta"
//
//	 */
//	// E.Coli
//	vector<std::string> paths = {
////    base_path + "EDL.fasta",
//			base_path + "genomes/Escherichia coli 536.fasta",
////    base_path + "HS.fasta",
//			base_path + "genomes/Escherichia coli 55989.fasta"
//	};
//	vector<std::string> suffices = {
//			"EC536",
//			"EC55989"
//	};
//
//	vector<size_t> k_sequence = { /*1001, 501, 201, 101, */55, 21, 15 };
//
//	/*
//	 std::string base_path = "/home/valich/work/human/";
//
//	 vector<std::string> paths = {
//	 base_path + "homo_sapiens_X.fasta",
//	 base_path + "pan_troglodytes_X.fasta"
//	 };
//	 vector<std::string> suffices = {
//	 "homo_sapiens_X",
//	 "pan_troglodytes_X"
//	 };
//	 vector<size_t> k_sequence = {
//	 101, 55, 21, 15
//	 };
//	 */
//
////	std::string files_md5 = utils::GenMD5FromFiles(paths);
////	INFO("result is stored with md5 of " << files_md5);
//
//	MaskDifferencesAndSave(paths, suffices,
//			//"bp_graph_" + files_md5 +
//            base_path + "processed/", k_sequence);
//}

BOOST_AUTO_TEST_CASE( TestGeneAnalysis ) {
    return;
	utils::TmpFolderFixture _("tmp");
	static size_t k = 25;
	typedef debruijn_graph::graph_pack<debruijn_graph::ConjugateDeBruijnGraph, LSeq> gp_t;
    gp_t gp(k, "tmp", Sequence(), 200, true);

    GeneCollection gene_collection;
    string root = "/home/snurk/Dropbox/olga_gelf/";
    gene_collection.Load(root, "genome_list.txt",
                         "/genomes/",
                         "gs.25ESS_ver3_sf_TN.csv",
                         "interesting_orthologs.txt");
    gene_collection.Update(gp);

    ColorHandler<gp_t::graph_t> coloring(gp.g);

    make_dir(root + "out/");

    WriteGeneLocality(gene_collection, gp, root + "out/", coloring);
}

BOOST_AUTO_TEST_CASE( MultipleGenomesVisualization ) {
	return;
	typedef debruijn_graph::graph_pack<
	/*Nonc*/debruijn_graph::ConjugateDeBruijnGraph, LSeq> comparing_gp_t;
	static const size_t K = 1001;
	utils::TmpFolderFixture _("tmp");

	std::string base_path = "bp_graph/refined/501/";

	// vector of pairs <name, path_to_fasta>
	/*
	 vector<pair<std::string, std::string> > genomes_paths = {
	 make_pair("MSSA476", "bp_graph_test/refined/mssa476.fasta"),
	 make_pair("MRSA252", "bp_graph_test/refined/mrsa252.fasta"),
	 make_pair("TW20", "bp_graph_test/refined/tw20.fasta"),
	 make_pair("USA300", "bp_graph_test/refined/usa300.fasta")
	 //    make_pair("11819", "bp_graph_test/refined/11819.fasta"),
	 //    make_pair("COL", "bp_graph_test/refined/COL.fasta")
	 };
	 */

	vector<pair<std::string, std::string> > genomes_paths = {
//    make_pair("EDL", base_path + "EDL.fasta"),
			make_pair("H6", base_path + "H6.fasta"),
//    make_pair("HS", base_path + "HS.fasta"),
			make_pair("K12", base_path + "K12.fasta"), make_pair("TW",
					base_path + "TW.fasta"), make_pair("UTI",
					base_path + "UTI.fasta")
//    make_pair("CCDC5079", base_path + "CCDC5079.fasta"),
//    make_pair("CCDC5180", base_path + "CCDC5180.fasta")
			};

	std::string folder = "bp_graph/multiple_genomes_visualization/";

	RunMultipleGenomesVisualization < comparing_gp_t
			> (K, genomes_paths, folder);
}

}

::boost::unit_test::test_suite* init_unit_test_suite(int, char*[]) {
	logging::logger *log = logging::create_logger("", logging::L_INFO);
	log->add_writer(std::make_shared<logging::console_writer>());
	logging::attach_logger(log);

	using namespace ::boost::unit_test;
	char module_name[] = "cap";

	assign_op(framework::master_test_suite().p_name.value,
			basic_cstring<char>(module_name), 0);

	omp_set_num_threads(1);

	return 0;
}
