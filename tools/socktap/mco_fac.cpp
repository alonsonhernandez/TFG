#include "mco_fac.hpp"
#include "logs_handler.hpp"
#include <cstring>
#include <iostream>
#include <ctime>
#include <cstdlib>
#include <vanetza/btp/ports.hpp>
#include <vanetza/asn1/cam.hpp>
#include <vanetza/asn1/packet_visitor.hpp>
#include <vanetza/facilities/cam_functions.hpp>
#include <boost/units/cmath.hpp>
#include <boost/units/systems/si/prefixes.hpp>
#include <chrono>
#include <exception>
#include <functional>
#include <iostream>

using namespace vanetza;
using namespace vanetza::facilities;
using namespace std::chrono;

using namespace Logging;

#define NOT_DEFINED -1;

#define ALPHA 0.016
#define BETA 0.0012
#define MCO_INTERVAL 200 //milliseconds
#define DELTA_BEGINNING 0.5
#define DELETED_TIME 1000000 //microseconds (1 second)
#define DELTA_OFFSET_MAX 0.0005
#define DELTA_OFFSET_MIN -0.00025
#define DELTA_MAX 0.03
#define DELTA_MIN 0.0006
#define DATA_SPEED 0.75 //bytes/microseconds (6 Mbps)
#define NOT_SEND 2000000 //microseconds (2 seconds)
#define SCALE 9
#define BTP_HEADER 4 //4
#define GEONETWORKING_HEADER 60 //60
# define INSTANT_SEND 1000 //1ms


McoFac::McoFac(PositionProvider& positioning, Runtime& rt) :
    positioning_(positioning), runtime_(rt), mco_interval_(milliseconds(MCO_INTERVAL)), adapt_delta(DELTA_BEGINNING) //mco interval es 200 ms
{
    schedule_timer();
}

McoFac::DataConfirm McoFac::mco_data_request(const DataRequest& request, DownPacketPtr packet, PortType PORT)
{
    
    LogsHandler* pLogger = NULL; // Create the object pointer for Logger Class
    pLogger = LogsHandler::getInstance();
    struct timeval te;
    gettimeofday(&te,NULL);
    std::stringstream ss;
    ss << "Transmission "<< PORT.get();
    pLogger->info(ss.str().c_str());

    DataConfirm confirm(DataConfirm::ResultCode::Rejected_Unspecified);

    byte_counter_update(packet->size());

    register_packet(PORT, packet->size(), std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    confirm = Application::request(request, std::move(packet), PORT);
    return confirm;
}

void McoFac::register_packet(PortType PORT, float msgSize, int64_t msgTime ){

    McoAppRegister* app_registered;

    for(McoAppRegister&iter : my_list){

        if(iter.PORT_ == PORT){
            
            app_registered = &iter;
            break;

        }

    }

    app_registered->msg_data_list.push_back({msgSize, msgTime});
    

}

void McoFac::register_app(PortType PORT, vanetza::Clock::duration& interval_,  Application& application){ 
            
    my_list.push_back(McoAppRegister(PORT, interval_, application));
    std::cout << "Se ha registrado la aplicacion con el puerto: " << PORT << std::endl;

    std::cout << "El numero de aplicaciones registradas es: " << my_list.size() << std::endl;

}

void McoFac::clean_outdated(){ // dejar siempre al menos 2 paquetes TODO
//recorre paquetes desde mas antiguo a mas reciente (FIFO)

    auto current_time = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    for(auto iter_app = my_list.begin() ; iter_app != my_list.end() ; iter_app++ ){

        for(auto iter_data = iter_app->msg_data_list.begin() ; iter_data != iter_app->msg_data_list.end() ;){

            if((current_time - iter_data->msgTime > DELETED_TIME)&&(iter_app->msg_data_list.size() > 2)){
                //si el paquete se envio hace mas de 1 s y hay mas de 2 registros en la lista, se borra
                iter_data = iter_app->msg_data_list.erase(iter_data);

            }
            else {

                ++iter_data;

            }

        }

    }

}

void McoFac::apps_average_size(){

    for(McoAppRegister& iter_app : my_list){

        int num_iter_data = 0;
        double data_sum = 0;

        for(auto iter_data : iter_app.msg_data_list ){
            //se añaden las cabeceras de niveles inferiores
            data_sum = data_sum + iter_data.msgSize + BTP_HEADER + GEONETWORKING_HEADER; 
            num_iter_data++;

        }

        if(num_iter_data != 0){
            iter_app.size_average = data_sum / num_iter_data;
        }

        
    }

}

void McoFac::apps_average_interval(){

    for(McoAppRegister& iter_app :  my_list){
        
        int num_iter_data = 0;
        int64_t data_sum = 0;

        for(auto iter_data = iter_app.msg_data_list.begin() ; iter_data != iter_app.msg_data_list.end(); iter_data++){
            
            auto iter_data_next = iter_data;
            iter_data_next++;

            if(iter_data_next != iter_app.msg_data_list.end()){
            
                data_sum = data_sum + (iter_data_next->msgTime - iter_data->msgTime);
                num_iter_data++;

            }

        }   
        if(num_iter_data != 0){
            iter_app.interval_average  = data_sum / num_iter_data;
        }
        
    }

}

void McoFac::calc_adapt_delta(){

    double delta_offset = BETA *(CBR_target - CBR);

    if(delta_offset < DELTA_OFFSET_MIN){
        delta_offset = DELTA_OFFSET_MIN;
    }
    if(delta_offset > DELTA_OFFSET_MAX){
        delta_offset = DELTA_OFFSET_MAX;
    }

    adapt_delta = (1 - ALPHA) * adapt_delta + delta_offset;

    if(adapt_delta > DELTA_MAX){
        adapt_delta = DELTA_MAX;
    }
    if(adapt_delta < DELTA_MIN){
        adapt_delta = DELTA_MIN;
    }

    LogsHandler* pLogger = NULL; // Create the object pointer for Logger Class
    pLogger = LogsHandler::getInstance();
    struct timeval te;
    gettimeofday(&te,NULL);
    long long current_time=te.tv_sec*1000LL+te.tv_usec/1000;
    std::stringstream ss;
    ss << "Delta " << adapt_delta ;
    pLogger->info(ss.str().c_str());
    
}

void McoFac::set_adapt_interval(){

    if(my_list.size() != 0){
        
        double ACRi = adapt_delta;   

        for(int i = 0; i < 4; i++){ //itera en cada traffic class

            double fraction_time = 0;
            
            for(auto iter_app : my_list){

                if(iter_app.traffic_class_ == i){

                    fraction_time += ((iter_app.size_average / DATA_SPEED) / (iter_app.min_interval)); // us / us
                    // fraccion de tiempo que la clase i pide
                }

            }

            if(fraction_time < ACRi){ //si la fraccion de tiempo que se pide es menor que la que se ofrece:

                for(McoAppRegister& iter_app : my_list){

                    if(iter_app.traffic_class_ == i){

                        apps_set_interval(iter_app, iter_app.min_interval);
                        
                    }

                }
                ACRi -= fraction_time;

            } else{ //si es mayor
                double CRi = 0;

                for(McoAppRegister& iter_app : my_list){ //Inicializacion de CRi

                    if((iter_app.traffic_class_ == i) && (iter_app.size_average > 0)){

                        double CREij = ((iter_app.size_average / DATA_SPEED) / (iter_app.interval_.count())); //TODO min_interval por media
                        //recursos consumidos por aplicacion j de traffic class i
                        CRi += CREij;
                        //recursos totales consumidos por traffic class i
                    }

                }

                for(McoAppRegister& iter_app : my_list){

                    if(iter_app.traffic_class_ == i){ 

                        double CREij;

                        double ACRij;

                        if(ACRi == 0 ){

                            apps_set_interval(iter_app, NOT_SEND); 

                        } else{
                            
                            CREij = ((iter_app.size_average / DATA_SPEED) / (iter_app.interval_.count())); //us / us

                            ACRij = (CREij / CRi) * ACRi;

                            int64_t Tonij = iter_app.size_average / DATA_SPEED; //us

                            int64_t Toffij = Tonij *((1 - ACRij) / ACRij); //us

                            apps_set_interval(iter_app, (Tonij + Toffij));
                        }
                        

                    }

                }

                ACRi = 0;
                
            }

        }

    }
    
}

int McoFac::rand_number(){

    srand(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count());

    return rand() % 4; 

}

Application& McoFac::search_port(vanetza::btp::port_type PORT){

    for(McoAppRegister& iter : my_list){

        if(iter.application_.port() == PORT){

            Application& application = iter.application_;

            return application;

        }

    }
    Application* application;

    return *application; //no deberia devolver nunca este, en cuyo caso, la aplicacion buscada no existe
}

void McoFac::byte_counter_update(unsigned packet_size){

    const unsigned header_size = BTP_HEADER+ GEONETWORKING_HEADER;
    packet_size += header_size;

    byte_counter += packet_size;

}

void McoFac::CBR_update(){

    double CBR_meassured = (byte_counter / DATA_SPEED)/mco_interval_.count(); //microseconds

    CBR_meassured *= SCALE;

    CBR = (CBR_meassured + CBR) * 0.5;

    LogsHandler* pLogger = NULL; // Create the object pointer for Logger Class
    pLogger = LogsHandler::getInstance();
    struct timeval te;
    gettimeofday(&te,NULL);
    long long current_time=te.tv_sec*1000LL+te.tv_usec/1000;
    std::stringstream ss;
    ss <<"CBR " << CBR;
    pLogger->info(ss.str().c_str());

    byte_counter = 0;

}

void McoFac::set_min_interval(vanetza::btp::port_type PORT, vanetza::Clock::duration interval){

    for(auto& iter_app : my_list){

        if(iter_app.PORT_ == PORT){

            iter_app.min_interval = interval.count();

        }
    }

}

void McoFac::set_apps_number(){

    for(McoAppRegister& iter_app : my_list){

        apps_number[iter_app.traffic_class_]++;
    }

}

void McoFac::set_traffic_class(int traffic_class, vanetza::btp::port_type PORT){

    for(auto& iter_app : my_list){

        if(iter_app.PORT_ == PORT){

            if(traffic_class < 4 && traffic_class >= 0){

                iter_app.traffic_class_ = traffic_class;
    
            } else{

                iter_app.traffic_class_ = 3;

            }

        }

    }

}

void McoFac::apps_set_interval(McoAppRegister &iter_app, int64_t update_interval){

    if(update_interval > NOT_SEND){
        update_interval = NOT_SEND;
    }

    int64_t present_time = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    
    if(present_time > iter_app.msg_data_list.back().msgTime + update_interval){
        iter_app.application_.set_interval(std::chrono::microseconds(INSTANT_SEND)); //para el temporizador y transmite al instante (1ms)
        
    }else{
        iter_app.application_.set_interval(std::chrono::microseconds(iter_app.msg_data_list.back().msgTime + update_interval - present_time));
        //para el temporizador y transmite en un tiempo total de update_interval
    }
    //entre el set interval y el interval_ = pasan 8-10 microsegundos
    iter_app.interval_ = std::chrono::microseconds(update_interval); // no para el temporizador, se actualiza el intervalo 

    LogsHandler* pLogger = NULL; // Create the object pointer for Logger Class
    pLogger = LogsHandler::getInstance();
    struct timeval te;
    gettimeofday(&te,NULL);
    long long current_time=te.tv_sec*1000LL+te.tv_usec/1000;
    std::stringstream ss;
    ss << "Interval "<< iter_app.PORT_ << " " << update_interval;
    pLogger->info(ss.str().c_str());

}

void McoFac::set_interval(Clock::duration interval)
{
    mco_interval_ = interval;
    runtime_.cancel(this);
    schedule_timer();
}


void McoFac::print_generated_message(bool flag)
{
    print_tx_msg_ = flag;
}

void McoFac::print_received_message(bool flag)
{
    print_rx_msg_ = flag;
}

McoFac::PortType McoFac::port()
{
    return btp::ports::MCO;
}

void McoFac::indicate(const DataIndication& indication, UpPacketPtr packet)
{   
    std::cout << "MCO received a packet" << std::endl;
    Application& application = search_port(indication.destination_port);

    if(indication.destination_port == btp::ports::CAM || indication.destination_port == btp::ports::CAM1 || indication.destination_port == btp::ports::CAM2
     || indication.destination_port == btp::ports::CAM3 || indication.destination_port == btp::ports::CAM4){

        asn1::PacketVisitor<asn1::Cam> visitor;
        std::shared_ptr<const asn1::Cam> cam = boost::apply_visitor(visitor, *packet);
        byte_counter_update(cam->size());
    }

    application.indicate(indication, std::move(packet));
}

void McoFac::schedule_timer()
{
    runtime_.schedule(mco_interval_, std::bind(&McoFac::on_timer, this, std::placeholders::_1), this);
}

void McoFac::on_timer(Clock::time_point)
{   
    
    schedule_timer();

    clean_outdated();

    apps_average_size();
    
    apps_average_interval();

    CBR_update();

    calc_adapt_delta();

    set_adapt_interval();

}