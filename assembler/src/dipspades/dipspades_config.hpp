#pragma once

#include "../include/config_singl.hpp"
#include <boost/property_tree/ptree_fwd.hpp>

struct dipspades_config {

	struct base_params {
	    size_t 			K;
	    bool 			use_multithreading;
	    size_t 			max_threads;
	    size_t 			max_memory;
	};

	struct io_params {
		std::string haplocontigs;
		size_t 		num_libraries;
		std::string log_filename;

	    std::string output_base;
	    std::string output_root;
	    std::string output_dir;
	    std::string output_suffix;
		std::string	output_saves;

		std::string dataset_name;

		std::string load_from;
	};

	struct run_params {
		std::string		entry_point;
		bool 			developer_mode;
	};

	struct polymorphic_br {
		bool 	enabled;
		double 	rel_bulge_length;
		double 	rel_bulge_align;
		size_t 	paired_vert_abs_threshold;
		double 	paired_vert_rel_threshold;
		size_t 	max_bulge_nucls_len;
		size_t	max_neigh_number;
		size_t 	num_iters_lbr;
		size_t 	num_iters_hbr;
	};

	struct consensus_constructor {
		bool 		enabled;
		double 		bulge_len_quantile;
		bool	 	tails_lie_on_bulges;
		bool	 	align_bulge_sides;
		bool 		estimate_tails;
		size_t	 	min_overlap_size;
		size_t 		min_lcs_size;
		size_t 		max_loop_length;
	};

	struct haplotype_assembly {
		bool 		enabled;
	};

	base_params 			bp;
	io_params				io;
	run_params				rp;
	polymorphic_br 			pbr;
	consensus_constructor 	cc;
	haplotype_assembly 		ha;
};

void load(dipspades_config &cfg, std::string const &filename);

typedef config_common::config<dipspades_config> dsp_cfg;
