xtern "C"
{
    #define restrict __restrict
    #include "main.h"

    #include <limits.h>
    #include <pthread.h>
    #include <container/hash.h>
    #include <algoutil/hints.h>
    #include <algoutil/defaults.h>
    #include <algoutil/lbm_topic_helpers.h>
    #include <algoutil/pool.h>
    #include <algoutil/hashvalue.h>
    #include <algoutil/debug.h>
    #include <stdlib.h>
    #include <stdio.h>
    #include <ctype.h>
    #include <ttsdk_host_util/params.h>
    #include <ttsdk/internal/param.h>
}

//  lbm/lbm_cpp.h MUST be included ahead of the algo logger because the latter
//  defines LogLevel that conflicts with the Debesys logger definitions used in lbm/lbm_cpp.h
#include <lbm/lbm_cpp.h>
#include <future>
#include <sys/stat.h>
#include <fcntl.h>
#include <tt/messaging/order/enums.pb.h>    //  for TTMarketID - include BEFORE lbm_sender_transaction_logger.h
#include "ttstl/task_queue.h"
#include <lbm/lbm_sender_transaction_logger.h>
#include "comm.h"
#include "forwarder.h"
#include "protocol.h"
#include "book_downloader.h"
#include "logger.hpp"
#include <algoutil/config_xml.hpp>
#include <controller.hpp>

#include <functional>
#include <cstdint>
#include <algorithm>

#define RISK_TOPIC_STRING                       "RISK.%u"
#define ALGO_SENDER_TOPIC_STRING                "OR.ALGO.%s"
#define PATTERN_OC_TOPIC_STRING                 "(^PR\\.[0-9]+$|^OR\\.(OC\\.[0-9]+$|(ASE|AGGREGATOR|ALGO)\\.(.*)))"
#define ALGO_TOPIC_STRING                       "ALGO.%s"
#define BOOKIE_TOPIC_STRING                     "bookie.%s.req"
#define EDGESERVER_TOPIC_STRING                 "ES"
#define USER_GROUP_TOPIC_STRING                 "OR.UG"
#define OC_ORDERTAG_TOPIC_STRING                "OC.ORDERTAG.%llu"


using namespace tt::algoutil;
using namespace tt::algoserver;

//  copied from lbm.cpp
#define UMP_REGISTRATION_TIMEOUT_NS             NANOSECONDS_PER_SECOND*60

struct risk_channel_senders
{
    lbm::Source* source = nullptr;
    //  key - account ID
    std::unordered_map<unsigned long long, lbm::ChannelSource*> senders;
};

namespace {

    lbm::Context* s_lbm_context = nullptr;
    lbm::Receiver* s_recver = nullptr;
    lbm::Source* s_bookie_sender = nullptr;
    lbm::Source* s_side_channel_sender = nullptr;
    lbm::Receiver* s_side_channel_recver = nullptr;
    lbm::Receiver* s_edge_recver = nullptr;
    lbm::WildcardReceiver* s_oc_recver = nullptr;
    lbm::Source* s_user_group_sender = nullptr;
    lbm::Source* s_algo_sender = nullptr;
    unsigned int s_ump_registration_complete_count = 0;

    //  These two variables are used to keep track of requests
    std::shared_ptr<lbm::Request> s_bookie_download_request;
    //  algo instance ID is the key
    std::unordered_map<std::string, std::shared_ptr<lbm::Request>> s_algo_transfer_requests;
    //  key - market ID
    std::unordered_map<unsigned int, risk_channel_senders> s_risk_senders;

    //Algo Side channel sender
    std::unordered_map<unsigned long long, lbm::ChannelSource*> s_algo_senders;
    //Algo Side channel sender with Account spectrum
    std::unordered_map<unsigned long long, lbm::ChannelSource*> s_algo_sidechannel_sprectrum_senders;

    std::unordered_map<unsigned long long, lbm::ChannelSource*> s_user_group_senders;

    bool s_use_lbm_transaction_logger = true;
    std::shared_ptr<lbm::TransactionLogger> s_lbm_transaction_logger = nullptr;

    //  currently s_use_lbm_transaction_logger and s_use_ump are somewhat coupled.
    //  we can't have s_use_lbm_transaction_logger = true without having s_use_ump = true,
    //  although this may change in the future.
    //  For that reason, we keep two separate flags. The latter is used to indicate the current mode
    bool s_use_ump = true;

    // Order Tag
    std::unordered_map<unsigned long long /* market_id */, lbm::Source*> s_oc_ordertag_senders;
    std::unordered_map<tt::algoutil::ttuuid /* algo_inst_id */, std::shared_ptr<lbm::Request>> s_ordertag_requests;

} // anonymous namespace

extern std::unique_ptr<ConfigXml> s_algoConfig;

enum lbm_init_flags
{
    init_lbm_context                         = 0x00001,
    init_algo_receiver                       = 0x00002,
    init_algo_side_channel_receiver          = 0x00004,
    init_algo_side_channel_sender            = 0x00008,
    init_edge_receiver                       = 0x00010,
    init_algo_sender                         = 0x00020,
    init_bookie_sender                       = 0x00040,
    init_oc_receiver                         = 0x00080,
    init_user_group_sender                   = 0x00100,
    init_triage_action_env_receiver          = 0x00200,
    init_triage_action_env_app_receiver      = 0x00400,
    init_triage_action_env_app_inst_receiver = 0x00800,
    init_triage_action_env_app_dc_receiver   = 0x01000,
    //if need to add more flags, add it before init_all_succeeds
    init_all_succeeds                        = 0x10000,
};

//  may throw 
void
ChannelSenderSend (lbm::ChannelSource* channel_sender, const uint64_t timestamp, const char* encoded_data, const size_t encoded_size, const int flags);

switch (event)
    {
    case LBM_SRC_EVENT_CONNECT:
        TTLOG(INFO,13)<<"lbm: Source "<<std::string(topic_name)<<"connected to "<<std::string(static_cast<char*>(data));
        break;

    case LBM_SRC_EVENT_DISCONNECT:
        TTLOG(INFO,13)<<"lbm: Source "<<std::string(topic_name)<<"disconnected from "<<std::string(static_cast<char*>(data));
        break;

    case LBM_SRC_EVENT_UME_REGISTRATION_ERROR:
        TTLOG(INFO,13)<<"lbm: Error registering source with UME store: "<<std::string(static_cast<char*>(data));
        break;

    case LBM_SRC_EVENT_UME_REGISTRATION_SUCCESS:
        ume_registration = static_cast<lbm_src_event_ume_registration_t*>(data);

        TTLOG(INFO,13)<<"lbm: UME store registration success. RegID "<<ume_registration->registration_id<<"Source "<<std::string(topic_name);
        break;

    case LBM_SRC_EVENT_UME_REGISTRATION_SUCCESS_EX:
        LogUMERegistrationSuccess(static_cast<lbm_src_event_ume_registration_ex_t*>(data), "registration", topic_name);
        break;

    case LBM_SRC_EVENT_UME_DEREGISTRATION_SUCCESS_EX:
        LogUMERegistrationSuccess(static_cast<lbm_src_event_ume_registration_ex_t*>(data), "deregistration", topic_name);
        break;
