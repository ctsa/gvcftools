// -*- mode: c++; indent-tabs-mode: nil; -*-
//
// Copyright (c) 2009-2012 Illumina, Inc.
//
// Permission is hereby granted, free of charge, to any person
// obtaining a copy of this software and associated documentation
// files (the "Software"), to deal in the Software without
// restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
// BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
// ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
//

///
/// \author Chris Saunders
///

#include "compat_util.hh"
#include "gvcftools.hh"
#include "VcfHeaderHandler.hh"
#include "vcf_util.hh"


#include "boost/program_options.hpp"

//#include <ctime>

#include <iostream>
#include <string>


namespace {
std::ostream& log_os(std::cerr);
}

std::string cmdline;



struct CallRegionOptions {

    CallRegionOptions()
        : outfp(std::cout)
    {}

    std::ostream& outfp;
};



struct CallRegionVcfRecordHandler {

    CallRegionVcfRecordHandler(const CallRegionOptions& opt)
        : _opt(opt)
    {}

    ~CallRegionVcfRecordHandler() {
        // close out any remaining passed regions:
        writeCurrent();
    }

    void
    process_line(const istream_line_splitter& vparse) {
        const unsigned nw(vparse.n_word());

        if (nw != (VCFID::SAMPLE+1)) {
            log_os << "ERROR: unexpected number of fields in vcf record:\n";
            vparse.dump(log_os);
            exit(EXIT_FAILURE);
        }

        const char* filterStr(vparse.word[VCFID::FILT]);
        const bool isPassed(0==strcmp("PASS",filterStr));

        if (! isPassed) return;
        // extract begin end range -- submit to otuput processor

        unsigned begin_pos(0), end_pos(0);
        get_vcf_record_range(vparse.word, begin_pos, end_pos);

        // special check for insertions:
        if (end_pos+1 == begin_pos) return;

        assert(begin_pos > 0);
        assert(end_pos > 0);
        if (end_pos < begin_pos) {
            log_os << "ERROR: Can't parse record range. [begin,end] = " << begin_pos << "," << end_pos <<"\n";
            vparse.dump(log_os);
            exit(EXIT_FAILURE);
        }

        const char* chromStr(vparse.word[VCFID::CHROM]);
        addPassedRange(chromStr,begin_pos-1,end_pos);
    }

private:

    void
    writeCurrent() {
        if (_currentChrom.empty()) return;
        _opt.outfp << _currentChrom
                   << '\t' << _currentBeginPos
                   << '\t' << _currentEndPos
                   << '\n';
    }

    void
    updateCurrent(
        const char* chrom,
        const unsigned beginPos,
        const unsigned endPos) {

        writeCurrent();
        _currentChrom = chrom;
        _currentBeginPos = beginPos;
        _currentEndPos = endPos;
    }

    /// process an ordered set of passed range input into a bed file
    ///
    /// begin,end should be zero-indexed half-open
    void
    addPassedRange(
        const char* chrom,
        const unsigned beginPos,
        const unsigned endPos) {

        if (_currentChrom.empty()) {
            // initiallize values on first call:
            updateCurrent(chrom,beginPos,endPos);

        } else if (_currentChrom != chrom) {
            // start a new chrom:
            updateCurrent(chrom,beginPos,endPos);

        } else {
            assert(beginPos >= _currentBeginPos);

            if (beginPos > _currentEndPos) {
                updateCurrent(chrom,beginPos,endPos);
            } else {
                _currentEndPos = std::max(endPos,_currentEndPos);
            }
        }
    }

    const CallRegionOptions& _opt;

    std::string _currentChrom;
    unsigned _currentBeginPos;
    unsigned _currentEndPos;
};



static
void
process_vcf_input(const CallRegionOptions& opt,
                  std::istream& infp) {

    static const bool is_skip_header(true);
    VcfHeaderHandler header(opt.outfp, NULL, NULL, is_skip_header);
    CallRegionVcfRecordHandler rec(opt);

    istream_line_splitter vparse(infp);

    while (vparse.parse_line()) {
        if (header.process_line(vparse)) continue;
        rec.process_line(vparse);
    }
}



static
void
try_main(int argc,char* argv[]) {

    //const time_t start_time(time(0));
    const char* progname(compat_basename(argv[0]));

    for (int i(0); i<argc; ++i) {
        if (i) cmdline += ' ';
        cmdline += argv[i];
    }

    std::istream& infp(std::cin);
    CallRegionOptions opt;

    namespace po = boost::program_options;

    po::options_description help("help");
    help.add_options()
    ("help,h","print this message");

    po::options_description visible("options");
    visible.add(help);

    bool po_parse_fail(false);
    po::variables_map vm;
    try {
        po::store(po::parse_command_line(argc, argv, visible), vm);
        po::notify(vm);
    } catch (const boost::program_options::error& e) { // todo:: find out what is the more specific exception class thrown by program options
        log_os << "\nERROR: Exception thrown by option parser: " << e.what() << "\n";
        po_parse_fail=true;
    }

    if ((vm.count("help")) || po_parse_fail) {
        log_os << "\n" << progname << " creates a bed file of called regions from a gVCF\n\n";
        log_os << "version: " << gvcftools_version() << "\n\n";
        log_os << "usage: " << progname << " [options] < gVCF > called.bed\n\n";
        log_os << visible << "\n";
        exit(EXIT_FAILURE);
    }

    process_vcf_input(opt,infp);
}



static
void
dump_cl(int argc,
        char* argv[],
        std::ostream& os) {

    os << "cmdline:";
    for (int i(0); i<argc; ++i) {
        os << ' ' << argv[i];
    }
    os << std::endl;
}



int
main(int argc,char* argv[]) {

    std::ios_base::sync_with_stdio(false);

    // last chance to catch exceptions...
    //
    try {
        try_main(argc,argv);

    } catch (const std::exception& e) {
        log_os << "FATAL:: EXCEPTION: " << e.what() << "\n"
               << "...caught in main()\n";
        dump_cl(argc,argv,log_os);
        exit(EXIT_FAILURE);

    } catch (...) {
        log_os << "FATAL:: UNKNOWN EXCEPTION\n"
               << "...caught in main()\n";
        dump_cl(argc,argv,log_os);
        exit(EXIT_FAILURE);
    }
    return EXIT_SUCCESS;
}
