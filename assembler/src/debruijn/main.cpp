/*
 * Assembler Main
 */

#include "launch.hpp"
#include "config.hpp"
#include "common/logging.hpp"
#include "common/simple_tools.hpp"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "distance_estimation.hpp"
//#include <distance_estimation.hpp>

namespace {

std::string MakeLaunchTimeDirName() {
	time_t rawtime;
	struct tm * timeinfo;
	char buffer[80];

	time(&rawtime);
	timeinfo = localtime(&rawtime);

	strftime(buffer, 80, "%m.%d_%H_%M", timeinfo);
	return string(buffer);
}
}

DECL_PROJECT_LOGGER("d")

int main() {
	// check config.hpp parameters
	if (K % 2 == 0) {
		FATAL("K in config.hpp must be odd!\n");
	}
	checkFileExistenceFATAL(CONFIG_FILENAME);

	// read configuration file (dataset path etc.)
	string input_dir = CONFIG.read<string>("input_dir");
	string dataset = CONFIG.read<string>("dataset");
	string output_root = CONFIG.read<string>("output_dir");
	string output_dir_suffix = MakeLaunchTimeDirName()+ "." + dataset + "/";
	string output_dir = output_root + output_dir_suffix;
	string work_tmp_dir = output_root + "tmp/";
//	std::cout << "here " << mkdir(output_dir.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH| S_IWOTH) << std::endl;
	mkdir(output_dir.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH | S_IWOTH);
	string genome_filename = input_dir
			+ CONFIG.read<string>("reference_genome");
	string reads_filename1 = input_dir + CONFIG.read<string>(dataset + "_1");
	string reads_filename2 = input_dir + CONFIG.read<string>(dataset + "_2");
	checkFileExistenceFATAL(genome_filename);
	checkFileExistenceFATAL(reads_filename1);
	checkFileExistenceFATAL(reads_filename2);

	size_t insert_size = CONFIG.read<size_t>(dataset + "_IS");
	size_t max_read_length = 100; //CONFIG.read<size_t> (dataset + "_READ_LEN");
	int dataset_len = CONFIG.read<int>(dataset + "_LEN");
	bool paired_mode = CONFIG.read<bool>("paired_mode");
    bool rectangle_mode  = CONFIG.read<bool>("rectangle_mode");
	bool etalon_info_mode = CONFIG.read<bool>("etalon_info_mode");
	bool from_saved = CONFIG.read<bool>("from_saved_graph");
	// typedefs :)
  typedef io::Reader<io::SingleRead> ReadStream;
  typedef io::Reader<io::PairedRead> PairedReadStream;
  typedef io::RCReaderWrapper<io::PairedRead> RCStream;

	// read data ('reads')
  PairedReadStream pairStream(std::pair<std::string, 
                              std::string>(reads_filename1,
                                           reads_filename2),
                              insert_size);
  	ReadStream reads_1(reads_filename1);
  	ReadStream reads_2(reads_filename2);
  	vector<ReadStream*> reads;
  	reads.push_back(&reads_1);

  	reads.push_back(&reads_2);
// 	reads.push_back(reads_2);
  	//  	reads[1] = reads_1;
//  	reads[2] = reads_2;
  	RCStream rcStream(&pairStream);

	// read data ('genome')
	std::string genome;
	{
		ReadStream genome_stream(genome_filename);
    io::SingleRead full_genome;
		genome_stream >> full_genome;
		genome = full_genome.GetSequenceString().substr(0, dataset_len); // cropped
	}
	// assemble it!
	INFO("Assembling " << dataset << " dataset");
	debruijn_graph::DeBruijnGraphTool<K, RCStream>(rcStream, Sequence(genome), paired_mode, rectangle_mode, etalon_info_mode, from_saved, insert_size, max_read_length, output_dir, work_tmp_dir, reads);
	INFO("Assembling " << dataset << " dataset finished");

	unlink((output_root + "latest").c_str());
	if (symlink(output_dir_suffix.c_str(), (output_root + "latest").c_str()) != 0)
		WARN( "Symlink to latest launch failed");

	// OK
	return 0;
}

