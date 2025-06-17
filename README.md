# V2X Congestion Control for Multi-Channel Operation over Vanetza 

This repository contains the open-source code developed for the following paper:

    Miguel Sepulcre, Yeray Guadalcazar, Miguel A. Fornell, Gokulnath Thandavarayan, Francisco Paredes Vera, Javier Gozalvez, Amir Mohammadisarab, 
    "V2X Congestion Control for Multi-Channel Operation: a Scalable Validation in Virtualized Environments", 
    Proc. IEEE 101st Vehicular Technology Conference (VTC2025-Spring), Oslo, (Norway), 17-20 June 2025.
 
This paper presents the design, implementation, and extensive validation of a Facilities layer V2X congestion control solution for multi-channel operation integrated into [Vanetza](https://github.com/riebl/vanetza). Our approach dynamically adapts transmission parameters based on real-time channel conditions and the priorities and requirements of the V2X services operating in a C-ITS station. By employing a Traffic-Class based proportional fairness strategy, the solution allocates available communication resources among multiple V2X services, effectively responding to varying channel loads in real time. Scalable experimental results in a virtualized environment demonstrate that our solution meets ETSI Release 2 requirements while bridging the gap between simulation-based evaluations and real-world testing, accounting for hardware limitations and processing delays. This work lays a robust foundation for scalable and congestion-aware C-ITS testing and validation prior to real world deployments. 

The developments are provided as an extension of the [socktap](https://github.com/msepulcre/mcoVanetza/tree/desarrollo-mco/tools/socktap) tool.

Scripts are provided to run the [tests](https://github.com/msepulcre/mcoVanetza/tree/desarrollo-mco/tools/socktap/test) conducted in the paper.

## How to build

Follow the Vanetza instructions on [prerequisites](https://www.vanetza.org/how-to-build/#prerequisites) and [steps for compilation](https://www.vanetza.org/how-to-build/#compilation), and the [Docker](https://www.docker.com/) website.

