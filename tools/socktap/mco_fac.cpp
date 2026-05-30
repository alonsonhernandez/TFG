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
#include <iomanip> //para que funcione la funcion CBR update() y q se vea de forma gráfica
#include <cstdlib>// para que CBR update() y poder seleccionar entre escenarios
#include <iomanip>//
#include <fstream>//para recopilar los mensajes
#include <unistd.h> // Para usleep
#include <algorithm> // Para std::min
#include <cstdio> // Para std::fflush
#include <thread> // Para sleep_for
#include <mutex> // Para proteger la escritura asíncrona CSV

using namespace vanetza;
using namespace vanetza::facilities;
using namespace std::chrono;

using namespace Logging;

#define NOT_DEFINED -1;

#define ALPHA 0.016
#define BETA 0.0012
#define MCO_INTERVAL 200 //milliseconds
#define DELTA_BEGINNING 0.03
#define DELETED_TIME 1000000 //microseconds (1 second)
#define DELTA_OFFSET_MAX 0.0005
#define DELTA_OFFSET_MIN -0.00025
#define DELTA_MAX 0.03
#define DELTA_MIN 0.0006
//#define DATA_SPEED 0.75 //bytes/microseconds (6 Mbps), 0.75 de base 
#define NOT_SEND 2000000 //microseconds (2 seconds)
#define BTP_HEADER 4 //4
#define GEONETWORKING_HEADER 60 //60
# define INSTANT_SEND 1000 //1ms

// Función auxiliar para obtener la velocidad dinámicamente (en bits por segundo)
double get_current_data_speed() {
    const char* env_speed = std::getenv("DATA_SPEED");
    double speed_mbps = env_speed ? std::atof(env_speed) : 6.0;
    // Si la velocidad se configuró como 0.75, entendemos que era bytes/us
    // Las gráficas requerían 6 Mbps (6,000,000 bps)
    if (speed_mbps == 0.75) {
        speed_mbps = 6.0; 
    }
    return speed_mbps * 1000000.0; // Devolvemos bps (ej: 6,000,000)
}

// Función auxiliar para obtener el SCALE dinámicamente
double get_current_scale() {
    const char* env_scale = std::getenv("SCALE");
    return env_scale ? std::atof(env_scale) : 9.0;
}



McoFac::McoFac(PositionProvider& positioning, Runtime& rt) :
    positioning_(positioning), runtime_(rt)
{
    mco_interval_ = milliseconds(MCO_INTERVAL);
    adapt_delta = DELTA_BEGINNING;
    byte_counter_cch = 0;
    byte_counter_sch = 0;
    tx_byte_counter_cch = 0;
    tx_byte_counter_sch = 0;
    CBR_cch = 0.0;
    CBR_sch = 0.0;

    const char* env_host_ini = std::getenv("HOSTNAME");
    std::string host_str_ini = env_host_ini ? env_host_ini : "node-1";
    
    std::string csv_filename = "/app/vanetza/results/simulation_results_" + host_str_ini + ".csv";
    csv_file.open(csv_filename, std::ios::out); 
    if (csv_file.is_open()) {
        std::cout << "[MCO_LOG] ARCHIVO CREADO EXITOSAMENTE: " << csv_filename << std::endl;
        csv_file << "ID,Timestamp,CBR_CCH,CBR_SCH,Delta,Algorithm\n";
    }

    const char* env_mco_mode = std::getenv("MCO_MODE");
    std::string mco_mode = env_mco_mode ? env_mco_mode : "mco_dynamic";

    if (mco_mode == "cch_only") {
        /*
         * NOTA INFORMATIVA DE PUERTOS:
         * ---------------------------------------------
         * 2001: CAM (Cooperative Awareness Messages) - Latidos base de estado y seguridad (prioridad alta).
         * 2002: DENM (Decentralized Environmental Notification) - Alertas críticas y eventos (ej. accidente).
         * 2003: MAP (MapData) - Topología e info de geometría de intersecciones.
         * 2004: SPaT (Signal Phase and Timing) - Tiempos y estado de los semáforos.
         * 2005: IVI (In-Vehicle Information) - Información estructurada (señales de tráfico virtuales).
         * 3001, 4001, 5001, 6001: Puertos suplementarios usados en esta simulación como CAM1, CAM2, CAM3, CAM4
         *                         para representar otras ráfagas de datos periódicas o servicios no críticos.
         */
        for (int p : {2001, 2002, 2003, 2004, 2005, 3001, 4001, 5001, 6001}) channel_map[p] = 1;
    } else if (mco_mode == "sch_only") {
        for (int p : {2001, 2002, 2003, 2004, 2005, 3001, 4001, 5001, 6001}) channel_map[p] = 2;
    } else if (mco_mode == "mco_dynamic" || mco_mode == "mco_dinamic") {
        for (int p : {2001, 2002, 2003, 2004, 2005, 3001, 4001, 5001, 6001}) channel_map[p] = 1;
    } else {
        for (int p : {2001, 2002, 2003, 2004, 2005, 3001, 4001, 5001, 6001}) {
            channel_map[p] = (p == 2001) ? 1 : 2;
        }
    }

    const char* env_host = std::getenv("HOSTNAME");
    std::string hostname = env_host ? env_host : "node";
    
    // Todos los nodos imprimen para confirmar que están vivos en los logs de Docker
    std::cout << "[MCO_SYSTEM] Host: " << hostname << " | Modo de operación detectado: " << mco_mode << std::endl;

    schedule_timer();
}

McoFac::DataConfirm McoFac::mco_data_request(const DataRequest& request, DownPacketPtr packet, PortType PORT)
{
    // 1. Buscamos qué Traffic Class le asignamos a este puerto en el registro
    int assigned_tc = 3; // Valor por defecto (baja prioridad)
    for(const auto& iter : my_list) {
        if(iter.PORT_ == PORT) {
            assigned_tc = iter.traffic_class_;
            break;
        }
    }

    int forced_tc = assigned_tc;
    if (channel_map.count(PORT.host())) {
        uint8_t mapped_channel = channel_map[PORT.host()];
        if (mapped_channel == 1) {
            forced_tc = (assigned_tc == 0 || assigned_tc == 1) ? assigned_tc : 1; 
        } else {
            forced_tc = (assigned_tc >= 2) ? assigned_tc : 3;
        }
    }

    // 2. Creamos una copia de la petición para poder modificar la clase de tráfico
    // Ya que 'request' es const y no se puede cambiar directamente
    DataRequest modified_request = request;
    modified_request.traffic_class = vanetza::geonet::TrafficClass(static_cast<uint8_t>(forced_tc));

    // 3. Ahora sí, actualizamos el contador transmitido con la clase forzada
    byte_counter_update(packet->size(), forced_tc, true);

    // Logs originales comentados para evitar saturación de CPU y asignaciones dinámicas por cada paquete
    // LogsHandler* pLogger = LogsHandler::getInstance();
    // std::stringstream ss;
    // ss << "Transmission Port: " << PORT.host() << " TC: " << forced_tc;
    // pLogger->info(ss.str().c_str());

    register_packet(PORT, packet->size(), std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    
    // IMPORTANTE: Enviamos la 'modified_request', no la original
    return Application::request(modified_request, std::move(packet), PORT);
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
// añadido
/*void McoFac::on_timer(Clock::time_point) {
    schedule_timer();
    clean_outdated();
    apps_average_size();
    apps_average_interval();
    CBR_update();
    calc_adapt_delta();
    set_adapt_interval();

    // Lógica de visualización inmediata de estado
    { (CBR > 0) &&
        std::cout << "[MCO_FAC] CBR: " << CBR << " | Delta: " << adapt_delta << std::endl;
    }
}
*/
// hasta aquí

void McoFac::update_channel_mapping(uint16_t port, uint8_t new_channel) {
    // Actualiza o inserta el canal para un puerto de aplicación en tiempo de ejecución
    channel_map[port] = new_channel;
}

void McoFac::register_app(PortType PORT, vanetza::Clock::duration& interval_,  Application& application){ 
            
    my_list.push_back(McoAppRegister(PORT, interval_, application));

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
	// añadido
	else {
            iter_app.size_average = 300;
	}//hasta aquí
        
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

void McoFac::calc_adapt_delta(bool traffic_diverted){

    const char* env_mco_mode = std::getenv("MCO_MODE");
    std::string mco_mode = env_mco_mode ? env_mco_mode : "mco_dynamic";
    
    double active_cbr = CBR_cch;
    if (mco_mode == "sch_only") {
        active_cbr = CBR_sch;
    }

    double delta_offset = BETA *(CBR_target - active_cbr);

    if(delta_offset < DELTA_OFFSET_MIN){
        delta_offset = DELTA_OFFSET_MIN;
    }
    if(delta_offset > DELTA_OFFSET_MAX){
        delta_offset = DELTA_OFFSET_MAX;
    }

    if (traffic_diverted) {
        delta_offset *= 0.5; // Suaviza el cambio para no ser tan brusco al liberar el CCH
    }

    adapt_delta = (1 - ALPHA) * adapt_delta + delta_offset;

    if(adapt_delta > DELTA_MAX){
        adapt_delta = DELTA_MAX;
    }
    if(adapt_delta < DELTA_MIN){
        adapt_delta = DELTA_MIN;
    }

    // Código de Logging innecesario que consumía llamadas al sistema repetitivas.
    // LogsHandler* pLogger = NULL; 
    // pLogger = LogsHandler::getInstance();
    // struct timeval te;
    // gettimeofday(&te,NULL);
    // long long current_time=te.tv_sec*1000LL+te.tv_usec/1000;
    // std::stringstream ss;
    // ss << "Delta " << adapt_delta ;
    // pLogger->info(ss.str().c_str());
    
}


void McoFac::set_adapt_interval() {
    if (my_list.size() == 0) return;

    double current_speed = get_current_data_speed();
    // Conexión del algoritmo DCC real (LIMERIC/MCO): T = Tamaño_Paquete / (delta * DataRate). 
    // Asumimos envíos medios de 400 bytes (3200 bits).
    double T_sec = 3200.0 / (adapt_delta * current_speed);
    
    // Lo convertimos a microsegundos 
    int64_t new_interval_us = static_cast<int64_t>(T_sec * 1000000.0);

    // Límite de seguridad superior para que nunca exceda los 2 segundos y crashee el scheduler
    if (new_interval_us > NOT_SEND) new_interval_us = NOT_SEND;
    
    // Límite de seguridad inferior (ETSI estándar T_GenCamMin = 100ms) 
    // Evita la emisión física a frecuencias suicidas (ej: 17ms) que provocaban el colapso de 5000ms en la CPU
    if (new_interval_us < 100000) new_interval_us = 100000;

    // Actualizamos el timer en base a delta de TODAS las apps
    for (auto &iter_app : my_list) {
        if (iter_app.msg_data_list.empty()) {
            iter_app.application_.set_interval(std::chrono::microseconds(new_interval_us));
            iter_app.interval_ = std::chrono::microseconds(new_interval_us);
        } else {
            apps_set_interval(iter_app, new_interval_us);
        }
    }
}


int McoFac::rand_number(){

    srand(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count());

    return std::rand() % 100;

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

void McoFac::byte_counter_update(unsigned packet_size, int traffic_class, bool is_tx){

    const unsigned header_size = BTP_HEADER + GEONETWORKING_HEADER;
    packet_size += header_size;

    const char* env_mco_mode = std::getenv("MCO_MODE");
    std::string mco_mode = env_mco_mode ? env_mco_mode : "mco_dynamic";

    if (mco_mode == "cch_only") {
        if (is_tx) tx_byte_counter_cch += packet_size;
        else byte_counter_cch += packet_size; 
        return; // Forzar que no haya tráfico espurio en SCH
    } else if (mco_mode == "sch_only") {
        if (is_tx) tx_byte_counter_sch += packet_size;
        else byte_counter_sch += packet_size;
        return; // Forzar que no haya tráfico espurio en CCH
    }

    // FASE 1 MULTICANAL: Clasificación 
    // 0 y 1 son seguridad (CCH), el resto servicios (SCH)
    if (traffic_class == 0 || traffic_class == 1) {
        if (is_tx) tx_byte_counter_cch += packet_size;
        else byte_counter_cch += packet_size; // Solo para seguridad (RX)
    } else {
        if (is_tx) tx_byte_counter_sch += packet_size;
        else byte_counter_sch += packet_size; // Solo para servicios (RX)
    }

}

// función modificada para mayor control de las pruebas

void McoFac::CBR_update() {
    // 1. PARÁMETROS:
    double current_speed = get_current_data_speed();
    
    // 2. CÁLCULO DEL CBR CON SCALE DEFINITIVO:
    double scale = get_current_scale();
    double CBR_measured_cch = (static_cast<double>(byte_counter_cch) * 8.0 * scale) / (current_speed * 0.2);
    double CBR_measured_sch = (static_cast<double>(byte_counter_sch) * 8.0 * scale) / (current_speed * 0.2);
    
    // 3. SUAVIZADO (Media móvil):
    // Promediamos el valor actual con el anterior y limitamos el máximo de canal a 1.0.
    CBR_cch = std::min((CBR_measured_cch + CBR_cch) * 0.5, 1.0);
    CBR_sch = std::min((CBR_measured_sch + CBR_sch) * 0.5, 1.0);
    CBR = CBR_cch; // El algoritmo de control usa el canal de seguridad

    // 4. VISUALIZACIÓN POR CONSOLA (Barras de colores):
    // ELIMINADAS: Solo usamos el reporte cada 2 segundos dictado en on_timer para no asfixiar la CPU.

    // 5. GUARDADO EN CSV ASÍNCRONO (Crucial para WSL2 donde la escritura a Windows bloquea milisegundos):
    if (csv_file.is_open()) {
        const char* host_name = std::getenv("HOSTNAME");
        std::string host_str = host_name ? host_name : "node-1";
        
        auto now = std::chrono::system_clock::now().time_since_epoch().count();
        const char* env_mco_mode = std::getenv("MCO_MODE");
        std::string mode = env_mco_mode ? env_mco_mode : "mco_dynamic";

        // Formatear el texto antes de lanzarlo
        std::string csv_line = host_str + "," + std::to_string(now) + "," + 
                               std::to_string(CBR_cch) + "," + std::to_string(CBR_sch) + "," + 
                               std::to_string(adapt_delta) + "," + mode + "\n";
        
        // Despachamos un hilo independiente para guardar los datos
        std::thread([this, csv_line]() {
            this->csv_file << csv_line;
            this->csv_file.flush();
        }).detach();
    }

    // 6. REINICIO DE TODOS LOS CONTADORES (Fija fugas de estado base):
    byte_counter_cch = 0;
    byte_counter_sch = 0;
    tx_byte_counter_cch = 0;
    tx_byte_counter_sch = 0;
    
    // Forzamos el vaciado del buffer para asegurar visibilidad inmediata en logs
    std::fflush(stdout);
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

    // LogsHandler* pLogger = NULL; // Create the object pointer for Logger Class
    // pLogger = LogsHandler::getInstance();
    // struct timeval te;
    // gettimeofday(&te,NULL);
    // long long current_time=te.tv_sec*1000LL+te.tv_usec/1000;
    // std::stringstream ss;
    // ss << "Interval "<< iter_app.PORT_ << " " << update_interval;
    // pLogger->info(ss.str().c_str());

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
    struct SizeVisitor : public boost::static_visitor<std::size_t> {
        std::size_t operator()(const CohesivePacket& p) const { return p.size(); }
        std::size_t operator()(const ChunkPacket& p) const { return p.size(); }
    };
    std::size_t payload_size = boost::apply_visitor(SizeVisitor(), *packet);

    byte_counter_update(payload_size, static_cast<int>(indication.traffic_class.raw()), false);

    Application& application = search_port(indication.destination_port);
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

    const double CBR_THRESHOLD = 0.68;
    const double CBR_RECOVERY_THRESHOLD = 0.58;
    static bool traffic_diverted = false;

    const char* env_mco_mode = std::getenv("MCO_MODE");
    std::string mco_mode = env_mco_mode ? env_mco_mode : "mco_dynamic";

    if (mco_mode == "mco_dynamic" || mco_mode == "mco_dinamic") {
        if (CBR_cch > CBR_THRESHOLD && !traffic_diverted) {
            for (auto const& pair : channel_map) {
                if (pair.first != 2001) {
                    update_channel_mapping(pair.first, 2);
                }
            }
            std::string hname = std::getenv("HOSTNAME") ? std::getenv("HOSTNAME") : "";
            std::cout << "[MCO_ALGO] [" << hname << "] Umbral superado. Desviando tráfico al SCH.\n";
            traffic_diverted = true;
        } else if (CBR_cch < CBR_RECOVERY_THRESHOLD && traffic_diverted) {
            for (auto const& pair : channel_map) {
                update_channel_mapping(pair.first, 1);
            }
            std::string hname = std::getenv("HOSTNAME") ? std::getenv("HOSTNAME") : "";
            std::cout << "[MCO_ALGO] [" << hname << "] Canal despejado. Recuperando tráfico al CCH.\n";
            traffic_diverted = false;
        }
    }

    calc_adapt_delta(traffic_diverted);
    set_adapt_interval();
}
