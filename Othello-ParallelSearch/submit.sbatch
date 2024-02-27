#!/bin/bash
#SBATCH --export=ALL
#SBATCH --nodes=1 
#SBATCH --ntasks=1
#SBATCH --ntasks-per-node=1 
#SBATCH --cpus-per-task=16 
#SBATCH --mem-per-cpu=512
#SBATCH --threads-per-core=1
#SBATCH --time=00:30:00 
#SBATCH --partition=interactive
#SBATCH --reservation=classroom

for ((i = 1; i <= 32; i++)); do 
  CILK_NWORKERS=$i time ./othello < default_input 
done
