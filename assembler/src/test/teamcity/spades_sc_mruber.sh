#!/bin/bash

############################################################################
# Copyright (c) 2011-2012 Saint-Petersburg Academic University
# All Rights Reserved
# See file LICENSE for details.
############################################################################

set -e
pushd ../../../

rm -rf /tmp/data/output/spades_output/MRUBER

#./spades_compile.sh
./spades.py --sc -m 160 -k 21,33,55 --12 data/input/M.ruber/Mru_no9.fastq -o /tmp/data/output/spades_output/MRUBER

rm -rf ~/quast-1.3/MRUBER/

python ~/quast-1.3/quast.py -R data/input/E.coli/MG1655-K12.fasta.gz -G data/input/E.coli/genes/genes.gff -O data/input/E.coli/genes/operons.gff -o ~/quast-1.3/MRUBER/ /tmp/data/output/spades_output/MRUBER/contigs.fasta

python src/test/teamcity/assess.py ~/quast-1.3/MRUBER/transposed_report.tsv 49100 16 2152 75.6 65 20
exitlvl=$?
popd
