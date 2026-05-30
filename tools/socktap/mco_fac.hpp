#ifndef MCO_FAC_HPP_PSIGPUTG
#define MCO_FAC_HPP_PSIGPUTG

#include "mco_app_register.hpp"
#include "application.hpp"
#include <iostream>
#include <list>
#include <cstring>
#include <string.h>
#include <chrono>
#include <vanetza/common/clock.hpp>
#include <vanetza/common/position_provider.hpp>
#include <vanetza/common/runtime.hpp>

#include <fstream>
#include <map>
#include <cstdint>
#include <vector>

class McoFac : public Application
{
public:

    using DataRequest = vanetza::btp::DataRequestGeoNetParams;
    using DataConfirm = vanetza::geonet::DataConfirm;
    using DownPacketPtr = vanetza::geonet::Router::DownPacketPtr;

    
    McoFac(vanetza::PositionProvider& positioning, vanetza::Runtime& rt); 

    DataConfirm mco_data_request(const DataRequest&, DownPacketPtr, PortType PORT); 

    void register_app(PortType PORT ,vanetza::Clock::duration& interval_, Application& application);

    void update_channel_mapping(uint16_t port, uint8_t new_channel);

    void register_packet(PortType PORT, float msgSize, int64_t msgTime);
    
    void clean_outdated();

    void apps_average_size();
    
    void apps_average_interval();

    void calc_adapt_delta(bool traffic_diverted = false);

    void set_adapt_interval();

    int rand_number();

    Application& search_port(vanetza::btp::port_type PORT);

    void byte_counter_update(unsigned packet_size, int traffic_class, bool is_tx = false);

    void CBR_update();

    void set_min_interval();

    void set_min_interval(vanetza::btp::port_type PORT, vanetza::Clock::duration interval);

    void set_apps_number();

    void set_traffic_class(int traffic_class);

    void set_traffic_class(int traffic_class, vanetza::btp::port_type PORT);

    void apps_set_interval(McoAppRegister& iter_app, int64_t update_interval);

    double adapt_delta;

    double CBR_target;

    double CBR; // ya no usado porque tenemos uno específico por canal

    double CBR_cch;   // Termómetro para Canal de Control
    
    double CBR_sch;  // Termómetro para Canal de Servicio

    unsigned byte_counter_cch; 
    
    unsigned byte_counter_sch;

    unsigned tx_byte_counter_cch; 
    
    unsigned tx_byte_counter_sch;

    unsigned byte_counter; // ya no usado porque tenemos uno específico por canal

    int apps_number[4] = {0, 0, 0, 0};
    
    PortType port() override;
    void indicate(const DataIndication&, UpPacketPtr) override;
    void set_interval(vanetza::Clock::duration);
    void print_received_message(bool flag);
    void print_generated_message(bool flag);

    std::list<McoAppRegister> my_list;
    
    // Mapa dinámico de canales (Puerto -> Canal)
    std::map<uint16_t, uint8_t> channel_map;

  
/* protected:
    
    vanetza::geonet::Router* router_;
    vanetza::geonet::GbcDataRequest request_gbc(const DataRequest&);
    vanetza::geonet::ShbDataRequest request_shb(const DataRequest&); */
    
private:
    void schedule_timer();
    void on_timer(vanetza::Clock::time_point);

    vanetza::PositionProvider& positioning_;
    vanetza::Runtime& runtime_;
    vanetza::Clock::duration mco_interval_;
    bool print_rx_msg_ = false;
    bool print_tx_msg_ = false;

    std::ofstream csv_file;
};

#endif /* MCO_FAC_HPP_EUIC2VFR */
