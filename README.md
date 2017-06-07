# arithmetic-coding
* A codec that uses fast arithmetic coding with adaptive model and context for (ASCII) text file compression and decompression.
* The program uses fast arithmetic coding with an adaptive data model with context for the compression.
* It's made to compress text files with a 256 character alphabet (ASCII).

## To build the project with g++ use:
	1. build.bat
	or
	2. build.sh

The resulting executable file can be found in the "bin" folder.

## Usage
###### The executable can be used through it's CLI as follows:
  1.	ArithmeticCodeCodec -c data_file_name compressed_file_name
  2.	ArithmeticCodeCodec -d compressed_file_name new_file_name


## Testing
* In the "test" folder there are some input files for good and bad cases of compression and a large file.
* In the "test" folder there is also a "acc_test.bat" file with which tests can be run under windows on the provided input files showing the time for compression and decompression as well as the compression rate (input file / compressed file).
* To run the tests you must first put the resulting executable from the build in the "test" folder.
