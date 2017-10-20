# Compute the actual repartition of nodes between the different application classes:
# We have 50000 nodes
N := 50000;
# We want 66% / 5.5% / 15.5% and 12.5% overall
workload := [.66, 0.055, .155, .12];
# Each app has q[i] nodes and lasts for wall[i] hours
wall := [262.4, 64, 128, 157.2];
q := [16384*(1/16), 4096*(1/16), 32768*(1/16), 30000*(1/16)];
# ress[i] represents the CPU.h used by an application of class i
ress := [q[1]*wall[1], q[2]*wall[2], q[3]*wall[3], q[4]*wall[4]];
# t represents the resource necessary to run one app of each class
t := sum(ress[i], i = 1 .. 4);
# ressratio[i] is the proportion of CPU.h neede to run one app of class i
ressratio := [ress[1]/t, ress[2]/t, ress[3]/t, ress[4]/t];
# nratio[i] is the relative number of applications of class i to run in order to get the workload goal
nratio := [workload[1]/ressratio[1], workload[2]/ressratio[2], workload[3]/ressratio[3], workload[4]/ressratio[4]];
# m is a scaling factor so that the total number of nodes is N
m := N/evalf(sum(q[i]*nratio[i], i = 1 .. 4));
# n[i] is thus the number of applications of class i that we run in steady state
n := m*nratio;

# Waste(C, mu) computes Equation (7) of the paper
Waste := proc (C, mu)
   local P, r, Period;
   # Pi is computed for each qi/Ci following equation (8)
   P := [sqrt(2*mu*N*(q[1]/N+L)*C[1]/q[1]^2), sqrt(2*mu*N*(q[2]/N+L)*C[2]/q[2]^2), sqrt(2*mu*N*(q[3]/N+L)*C[3]/q[3]^2), sqrt(2*mu*N*(q[4]/N+L)*C[4]/q[4]^2)];
   # r is the lambda that solves equation (6), assuming we want the smallest lambda that is still positive
   r := fsolve(sum(n[i]*C[i]/P[i], i = 1 .. 4)-1 = 0, L);
   # Periodi is the numerical value corresponding to equation (8) with lambda instanciated
   Period := eval(P, L = min(0, r));
   # and we return Equation (7) for the computed Periodi
   return evalf(sum(n[i]*q[i]*(C[i]/Period[i]+q[i]*((1/2)*Period[i]+C[i])/mu)/N, i = 1 .. 4))
end proc;

# We extracted the checkpoint duration for each application class at 1 TB/s from the simulation trace
# and scale up the checkpoint time for variable bandwidth
CkptTime := proc (bw)
    return [43.58144*1e12/bw, 12.597*1e12/bw, 190.67*1e12/bw, 42.389*1e12/bw]
end proc;

# For each bandwidth in the figure, and each MTBF in the figure, compute the theoretical waste and store it into model.dat
fd := fopen("prospective.dat", WRITE);
for M from 5 to 24 do
    # M is the node MTBF in hours
    min_bw := 1e11;
    max_w := Waste(CkptTime(min_bw), M*24*365);
    max_bw := 1e12;
    min_w := Waste(CkptTime(max_bw), M*24*365);
    while min_w > 0.8 do
    	  min_bw := max_bw;
	  max_w  := Waste(CkptTime(min_bw), M*24*365);
	  max_bw := 10*max_bw;
	  min_w  := Waste(CkptTime(max_bw), M*24*365);
    end do;
    while (max_w > 0.801) or (min_w < 0.799) do
    	  bw := (max_bw + min_bw) / 2.0;
	  w  := Waste(CkptTime(bw), M*24*365);
    	  if w < 0.8 then
	     max_bw := bw;
	  else
	     min_bw := bw;
	  end if;
	  max_w := Waste(CkptTime(min_bw), M*24*365);
    	  min_w := Waste(CkptTime(max_bw), M*24*365);
    end do;
    fprintf(fd, "%g\t%g\t%g\n", bw, M, Waste(CkptTime(bw), M*24*365))
end do;
fclose(fd);
