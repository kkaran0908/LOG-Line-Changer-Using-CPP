extern "C"
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

//  copied from lbm.cpp
static void
LogUMERegistrationSuccess (
    lbm_src_event_ume_registration_ex_t* ume_registration_ex,
    const std::string& function_string,
    const std::string& topic_name)
{
    TTLOG(INFO,13)<<"        lbm: UME store "<<ume_registration_ex->store_index<<": "<<ume_registration_ex->store<<" "<<function_string<<" success. RegID "<<ume_registration_ex->registration_id<<", Flags "<<ume_registration_ex->flags<<" "<<ume_registration_ex->flags&LBM_SRC_EVENT_UME_REGISTRATION_SUCCESS_EX_FLAG_OLD?"+":"-"<<"OLD[SQN "<<ume_registration_ex->sequence_number<<"] "<<ume_registration_ex->flags&LBM_SRC_EVENT_UME_REGISTRATION_SUCCESS_EX_FLAG_NOACKS?"+":"-"<<"NOACKS Source "<<topic_name<<endl;
}

static void
SenderCallback (int event, void* data, std::string topic_name)
{
    lbm_src_event_ume_registration_t* restrict             ume_registration;
    lbm_src_event_ume_registration_complete_ex_t* restrict ume_registration_complete;
    lbm_src_event_ume_ack_ex_info_t* restrict              ume_ack_ex_info;
    lbm_src_event_flight_size_notification_t* restrict     flight_size_notification;
    std::string                                            flight_size_notification_type;
    std::string                                            flight_size_notification_state;
    std::string                                            msg_clientd;

    switch (event)
    {
    case LBM_SRC_EVENT_CONNECT:
        TTLOG(INFO,13)<<"lbm: Source "<<topic_name<<" connected to "<<static_cast<char*>(data)<<endl;
        break;

    case LBM_SRC_EVENT_DISCONNECT:
        TTLOG(INFO,13)<<"lbm: Source "<<topic_name<<" disconnected from "<<static_cast<char*>(data)<<endl;
        break;

    case LBM_SRC_EVENT_UME_REGISTRATION_ERROR:
        TTLOG(INFO,13)<<"lbm: Error registering source with UME store: "<<static_cast<char*>(data)<<endl;
        break;

    case LBM_SRC_EVENT_UME_REGISTRATION_SUCCESS:
        ume_registration = static_cast<lbm_src_event_ume_registration_t*>(data);

        TTLOG(INFO,13)<<"lbm: UME store registration success. RegID "<<ume_registration->registration_id<<" Source "<<topic_name<<endl;
        break;

    case LBM_SRC_EVENT_UME_REGISTRATION_SUCCESS_EX:
        LogUMERegistrationSuccess(static_cast<lbm_src_event_ume_registration_ex_t*>(data), "registration", topic_name);
        break;

    case LBM_SRC_EVENT_UME_DEREGISTRATION_SUCCESS_EX:
        LogUMERegistrationSuccess(static_cast<lbm_src_event_ume_registration_ex_t*>(data), "deregistration", topic_name);
        break;

    case LBM_SRC_EVENT_UME_DEREGISTRATION_COMPLETE_EX:
        TTLOG(INFO,13)<<"lbm: UME DEREGISTRATION IS COMPLETE_EX Source "<<topic_name<<endl;
        break;

    case LBM_SRC_EVENT_UME_REGISTRATION_COMPLETE_EX:
        __sync_fetch_and_add(&s_ump_registration_complete_count, 1);

        ume_registration_complete = static_cast<lbm_src_event_ume_registration_complete_ex_t*>(data);

        TTLOG(INFO,13)<<"lbm: UME registration complete. SQN "<<ume_registration_complete->sequence_number<<". Flags "<<ume_registration_complete->flags<<" "<<ume_registration_complete->flags&LBM_SRC_EVENT_UME_REGISTRATION_COMPLETE_EX_FLAG_QUORUM?"+":"-"<<"QUORUM Source "<<topic_name<<endl;
        break;

    case LBM_SRC_EVENT_UME_MESSAGE_NOT_STABLE:
        ume_ack_ex_info = static_cast<lbm_src_event_ume_ack_ex_info_t*>(data);

        msg_clientd = (ume_ack_ex_info->msg_clientd != nullptr) ? static_cast<char*>(ume_ack_ex_info->msg_clientd) : "nullptr";
        if (ume_ack_ex_info->flags & LBM_SRC_EVENT_UME_MESSAGE_NOT_STABLE_FLAG_STORE)
        {
            TTLOG(WARNING,13)<<"lbm: UME store "<<ume_ack_ex_info->store_index<<": "<<ume_ack_ex_info->store<<" message NOT stable!! SQN "<<ume_ack_ex_info->sequence_number<<" (cd "<<msg_clientd<<"). Flags "<<ume_ack_ex_info->flags<<" "<<ume_ack_ex_info->flags&LBM_SRC_EVENT_UME_MESSAGE_NOT_STABLE_FLAG_LOSS?"+":"-"<<"LOSS "<<ume_ack_ex_info->flags&LBM_SRC_EVENT_UME_MESSAGE_NOT_STABLE_FLAG_TIMEOUT?"+":"-"<<"TIMEOUT Source "<<topic_name<<endl;
        }
        else
        {
            TTLOG(WARNING,13)<<"lbm: UME message NOT stable!! SQN "<<ume_ack_ex_info->sequence_number<<" (cd "<<msg_clientd<<"). Flags "<<ume_ack_ex_info->flags<<" "<<ume_ack_ex_info->flags&LBM_SRC_EVENT_UME_MESSAGE_NOT_STABLE_FLAG_LOSS?"+":"-"<<"LOSS "<<ume_ack_ex_info->flags&LBM_SRC_EVENT_UME_MESSAGE_NOT_STABLE_FLAG_TIMEOUT?"+":"-"<<"TIMEOUT Source "<<topic_name<<endl;
        }
        break;

    case LBM_SRC_EVENT_UME_MESSAGE_RECLAIMED_EX:
        ume_ack_ex_info = static_cast<lbm_src_event_ume_ack_ex_info_t*>(data);

        msg_clientd = (ume_ack_ex_info->msg_clientd != nullptr) ? static_cast<char*>(ume_ack_ex_info->msg_clientd) : "nullptr";
        TTLOG(INFO,13)<<"lbm: UME message reclaimed (ex) - sequence number "<<ume_ack_ex_info->sequence_number<<" (cd "<<msg_clientd<<"). Flags "<<ume_ack_ex_info->flags<<" Source "<<topic_name<<endl;

        break;

    case LBM_SRC_EVENT_UME_STORE_UNRESPONSIVE:
        TTLOG(ERROR,13)<<"lbm: UME store: "<<static_cast<char*>(data)<<" Source "<<topic_name<<endl;
        break;

    case LBM_SRC_EVENT_FLIGHT_SIZE_NOTIFICATION:
        flight_size_notification = static_cast<lbm_src_event_flight_size_notification_t*>(data);

        switch (flight_size_notification->type)
        {
        case LBM_SRC_EVENT_FLIGHT_SIZE_NOTIFICATION_TYPE_UME:
            flight_size_notification_type = "UME";
            break;

        case LBM_SRC_EVENT_FLIGHT_SIZE_NOTIFICATION_TYPE_ULB:
            flight_size_notification_type = "ULB";
            break;

        case LBM_SRC_EVENT_FLIGHT_SIZE_NOTIFICATION_TYPE_UMQ:
            flight_size_notification_type = "UMQ";
            break;

        default:
            flight_size_notification_type = "unknown";
            break;
        }

        if (flight_size_notification->state == LBM_SRC_EVENT_FLIGHT_SIZE_NOTIFICATION_STATE_OVER)
            flight_size_notification_state = "OVER";
        else
            flight_size_notification_state = "UNDER";

        TTLOG(INFO,13)<<"lbm: Flight Size Notification. Type: "<<flight_size_notification_type<<". Inflight is "<<flight_size_notification_state<<" specified flight size Source "<<topic_name<<endl;
        break;

    default:
        break;
    }
}

lbm::Source* 
AlgoUtil_CreateUMPSender (
        char* const restrict                  topic_name,
        const char* const restrict            ump_ensemble
)
{
    lbm::SourceTopicAttributes attr;

    unsigned long long    start_ns;
    uint64_t              session_id;
    unsigned int          complete_count;
    lbm::Source* source = nullptr;

    session_id = AlgoUtil_CalculateSessionId(topic_name);

    TTLOG(INFO,13)<<"using the following string "<<topic_name<<" to generate session id: 0x"<<session_id<<"16llX"<<endl;

    try
    {
        attr.Set("ume_session_id", session_id);
    }
    catch (const lbm::LbmError& ex)
    {
        TTLOG(ERROR,13)<<"lbm: Failed to set ume_session_id, topic: "<<topic_name<<" error="<<ex.what()<<endl;
        return nullptr;
    }

    s_ump_registration_complete_count = 0;

    try
    {
        lbm::Source::Callback cbk = [=](int event, void* data) {SenderCallback(event, data, topic_name); };
        if (s_use_lbm_transaction_logger)
        {
            source = new lbm::SourceTL(s_lbm_transaction_logger,
                tt::messaging::order::enums::TTMarketID::TT_MARKET_ID_ALGO_INSTRUMENT, session_id, *s_lbm_context, topic_name, attr, cbk);
        }
        else
        {
            lbm::SourceTopic sender_topic(*s_lbm_context, topic_name, attr);
            source = new lbm::Source(*s_lbm_context, sender_topic, cbk);
        }
    }
    catch (const lbm::LbmError& ex)
    {
        TTLOG(ERROR,13)<<"Failed to create Source, topic: "<<topic_name<<" error="<<ex.what()<<endl;
        return nullptr;
    }

    start_ns = AlgoUtil_CurrentTimeNS();

    do
    {
        sched_yield();
        complete_count = __sync_fetch_and_add(&s_ump_registration_complete_count, 0);
    } while (
        complete_count == 0 &&
        AlgoUtil_CheckTimeTillTimeoutNS(start_ns, UMP_REGISTRATION_TIMEOUT_NS) > 0
        );

    if (complete_count == 0)
    {
        if (s_use_lbm_transaction_logger)
        {
            TTLOG(WARNING,13)<<"Timed out after waiting "<<(int)(UMP_REGISTRATION_TIMEOUT_NS/NANOSECONDS_PER_SECOND)<<" sec for UME_REGISTRATION_COMPLETE. Will rely on LBM_TRANSACTION_LOGGER to switch to LBM"<<endl;
        }
        else
        {
            TTLOG(ERROR,13)<<"Timed out after waiting "<<(int)(UMP_REGISTRATION_TIMEOUT_NS/NANOSECONDS_PER_SECOND)<<" sec for UME_REGISTRATION_COMPLETE"<<endl;
            delete source;
            return nullptr;
        }
    }

    return source;
}

static enum lbm_init_flags
InitLbmObjects (
                const char* restrict,
                const char* restrict,
                const char* restrict,
                const char* restrict,
                const char* restrict
                );

static enum algoserver__error_code
SendBookieRequest (const char*, size_t, lbm::RequestCallback cbk, std::shared_ptr<lbm::Request>& request);

static int
DownloadBookRequestCallback (const lbm::Message& message);

static int
TransferAlgoRequestCallback (const lbm::Message& message);

static int
DownloadOrderTagCallback (const lbm::Message& message);

static lbm::ChannelSource*
GetOrCreateAlgoChannelSender (unsigned long long account_id);

static lbm::ChannelSource*
GetOrCreateUserGroupChannelSender (unsigned long long user_group_id);

static lbm::Receiver* s_triage_action_env_recver = nullptr;
static lbm::Receiver* s_triage_action_env_app_recver = nullptr;
static lbm::Receiver* s_triage_action_env_app_inst_recver = nullptr;
static lbm::Receiver* s_triage_action_env_app_dc_recver = nullptr;

static lbm::ChannelSource*
GetOrCreateAlgoSideChannelSender (unsigned long long account_id);

static lbm::Source*
GetOrCreateOrderTagSender (unsigned long long market_id);

static void DataRecvd (const lbm::Message& msg)
{
    struct algoserver__event* event;
    struct algoserver__request* restrict request;
    size_t                               request_size;
    enum algoserver__error_code          error;

    switch (msg.Type())
    {
    case LBM_MSG_UNRECOVERABLE_LOSS:
        TTLOG(ERROR,13)<<"            LOST. topic="<<msg.TopicName()<<" source="<<msg.Source()<<" seq_num="<<msg.SequenceNumber()<<endl;
        break;

    case LBM_MSG_UNRECOVERABLE_LOSS_BURST:
        TTLOG(ERROR,13)<<"            LOST BURST. topic="<<msg.TopicName()<<" source="<<msg.Source()<<" seq_num="<<msg.SequenceNumber()<<endl;
        break;

    case LBM_MSG_BOS:
        TTLOG(DEBUG,13)<<"Beginning of transport session. Topic="<<msg.TopicName()<<" lbm_source="<<msg.Source()<<endl;
        break;

    case LBM_MSG_EOS:
        TTLOG(DEBUG,13)<<"End of transport session. Topic="<<msg.TopicName()<<" lbm_source="<<msg.Source()<<endl;
        break;

    case LBM_MSG_NO_SOURCE_NOTIFICATION:
        TTLOG(ERROR,13)<<"            No sources found for topic="<<msg.TopicName()<<"."<<endl;
        break;

    case LBM_MSG_UME_REGISTRATION_ERROR:
        TTLOG(ERROR,13)<<"            UME registration error: "<<endl;
        break;

    case LBM_MSG_UME_REGISTRATION_SUCCESS:
    {
        lbm_msg_ume_registration_t* reg = (lbm_msg_ume_registration_t*)(msg.Data());
        TTLOG(INFO,13)<<"            UME registration successful. topic="<<msg.TopicName()<<" source="<<msg.Source()<<" SrcRegID "<<reg->src_registration_id<<" RcvRegID "<<reg->rcv_registration_id<<"."<<endl;
    }
    break;

    case LBM_MSG_UME_REGISTRATION_SUCCESS_EX:
    {
        lbm_msg_ume_registration_ex_t* reg = (lbm_msg_ume_registration_ex_t*)(msg.Data());

        char sqn[PATH_MAX];
        memset(sqn, '\0', PATH_MAX);
        if (reg->flags & LBM_MSG_UME_REGISTRATION_SUCCESS_EX_FLAG_OLD)
        {
            snprintf(
                sqn,
                PATH_MAX,
                "OLD[SQN %x] ",
                reg->sequence_number
            );
        }

        char sid[PATH_MAX];
        memset(sid, '\0', PATH_MAX);
        if (reg->flags & LBM_MSG_UME_REGISTRATION_COMPLETE_EX_FLAG_SRC_SID)
        {
            snprintf(
                sid,
                PATH_MAX,
                "SrcSession ID 0x%lx",
                reg->src_session_id
            );
        }

        TTLOG(INFO,13)<<"            UME registration successful. topic="<<msg.TopicName()<<" source="<<msg.Source()<<" store "<<reg->store_index<<": "<<reg->store<<" SrcRegID "<<reg->src_registration_id<<" RcvRegID "<<reg->rcv_registration_id<<" Flags "<<reg->flags<<". "<<sqn<<" "<<reg->flags&LBM_MSG_UME_REGISTRATION_SUCCESS_EX_FLAG_NOCACHE?"+":"-"<<"NOCACHE "<<reg->flags&LBM_MSG_UME_REGISTRATION_SUCCESS_EX_FLAG_RPP?"+":"-"<<"RPP "<<sid<<endl;
    }
    break;

    case LBM_MSG_UME_REGISTRATION_COMPLETE_EX:
    {
        lbm_msg_ume_registration_complete_ex_t* reg = (lbm_msg_ume_registration_complete_ex_t*)(msg.Data());

        char sid[PATH_MAX];
        memset(sid, '\0', PATH_MAX);
        if (reg->flags & LBM_MSG_UME_REGISTRATION_COMPLETE_EX_FLAG_SRC_SID)
        {
            snprintf(
                sid,
                PATH_MAX,
                "SrcSession ID 0x%lx",
                reg->src_session_id
            );
        }

        TTLOG(INFO,13)<<"            UME registration complete. "<<endl;
    }
    break;

    case LBM_MSG_UME_DEREGISTRATION_SUCCESS_EX:
    {
        lbm_msg_ume_deregistration_ex_t* dereg = (lbm_msg_ume_deregistration_ex_t*)(msg.Data());

        char sqn[PATH_MAX];
        memset(sqn, '\0', PATH_MAX);
        if (dereg->flags & LBM_MSG_UME_REGISTRATION_SUCCESS_EX_FLAG_OLD)
        {
            snprintf(
                sqn,
                PATH_MAX,
                "OLD[SQN %x] ",
                dereg->sequence_number
            );
        }

        TTLOG(INFO,13)<<"            UME deregistration successful. "<<endl;
    }
    break;

    case LBM_MSG_UME_DEREGISTRATION_COMPLETE_EX:
        TTLOG(INFO,13)<<"            UME deregistration complete. topic="<<msg.TopicName()<<" source="<<msg.Source()<<endl;
        break;

    case LBM_MSG_UME_REGISTRATION_CHANGE:
        TTLOG(INFO,13)<<"            UME registration change. topic="<<msg.TopicName()<<" source="<<msg.Source()<<" data="<<msg.Data()<<endl;
        break;

    case LBM_MSG_DATA:
    case LBM_MSG_REQUEST:
    {
        event = (struct algoserver__event*)malloc(sizeof(struct algoserver__event));
        AlgoUtil_DebugInitBytes(event, sizeof(struct algoserver__event));

        request = &event->data.request;

        error = AlgoServer_DecodeRequest(
            msg.Data(),
            msg.Length(),
            request,
            &request_size,
            nullptr
        );

        if (ALGOUTIL__HINT_UNLIKELY(error != algoserver__error_none))
        {
            free(event);
            return;
        }

        if (request->id == algojob__request_cli_cmd && request->data.cli_cmd.is_triage)
        {
            // <Note on ownership of lbm_msg_t>
            // (1) lbm_msg_t is initially allocated on the call stack of the native LBM library callback "DataRecvd"
            // (2) In "DataRecvd", we take ownership of lbm_msg_t by calling lbm_msg_retain which increments ref count by 1.
            // (3) In "RespondToTriageCLI", we free lbm_msg_t by calling lbm_msg_delete which decrements ref count by 1.
            //  HACK:
            //  request->data.cli_cmd is defined in a C-language header file and therefore we cannot
            //  change the type of the lbm_msg data member to lbm::Message (by value).
            //  If we could, it would've been possible to rely on the ref counting implemented by the lbm::Message class
            //  (including assignment and copy constructor.)
            //  But because we can't change the data member type to lbm::Message,
            //  we have to operate on the naked lbm_msg_t held/owned by lbm::Message
            lbm::Message& non_const_msg = const_cast<lbm::Message&> (msg);
            //  get the naked lbm_msg_t from lbm::Message
            request->data.cli_cmd.lbm_msg = non_const_msg;
            lbm_msg_retain(request->data.cli_cmd.lbm_msg);
        }

        if (request->id == algojob__request_deploy_instr ||
            request->id == algojob__request_resume ||
            request->id == algojob__request_pause ||
            request->id == algojob__request_stop ||
            request->id == algojob__request_update ||
            request->id == algojob__request_deploy_held_order)
        {
            // Priority queue will hold critical user requests that originate from TTW client.
            AlgoServer_ForwardPriorityEvent(algoserver__event_request, request_size, event);
        }
        else if (request->id == algojob__request_export_value_snapshot)
        {
            //TTW sending lots of this, causing lots of noise in our log.
            AlgoServer_ForwardEvent(algoserver__event_request, 0, event);
        }
        else if (request->id == algojob__request_handle_user_disconnect)
        {
            std::thread([=]() {
                sleep(2);
                AlgoServer_ForwardEvent(algoserver__event_request, request_size, event);
                }).detach();
        }
        else
        {
            // Normal queue will hold:
            // 1. Init/DestroyClient Request
            // 2. PeriodicTask Request
            // 3. Algojob Responses, such as connector value updates
            // 4. Non-critical user requests, such as side-channel registration
            AlgoServer_ForwardEvent(algoserver__event_request, request_size, event);
        }

        break;
    }

    default:
        break;
    }
}


enum algoserver__error_code
AlgoServer_InitComm (
                     const char* app_name,
                     const char* data_center,
                     const char* algoserver__instance_identifier_name,
                     const char* inst_id,
                     const char* environment
                    )
{
    auto v = lbm_version();
    TTLOG(INFO,13)<<"lbm_version "<<v<<endl;
    enum lbm_init_flags init_flags = InitLbmObjects(app_name, data_center, algoserver__instance_identifier_name, inst_id, environment);
    if (init_flags & init_all_succeeds)
    {
        return algoserver__error_none;
    }
    else
    {
        if (init_flags & init_algo_side_channel_receiver)
        {
            delete s_side_channel_recver;
            s_side_channel_recver = nullptr;
            TTLOG(ERROR,13)<<"Clean up: side_channel_recver;"<<endl;
        }
        if (init_flags & init_algo_side_channel_sender)
        {
            delete s_side_channel_sender;
            s_side_channel_sender = nullptr;
            TTLOG(ERROR,13)<<"Clean up: side_channel_sender;"<<endl;
        }
        if (init_flags & init_algo_receiver)
        {
            delete s_recver;
            s_recver = nullptr;
            TTLOG(ERROR,13)<<"Clean up: recver;"<<endl;
        }
        if (init_flags & init_edge_receiver)
        {
            delete s_edge_recver;
            s_edge_recver = nullptr;
            TTLOG(ERROR,13)<<"Clean up: edge_recver;"<<endl;
        }
        if (init_flags & init_oc_receiver)
        {
            delete s_oc_recver;
            s_oc_recver = nullptr;
            TTLOG(ERROR,13)<<"Clean up: oc_recver;"<<endl;
        }
        if (init_flags & init_bookie_sender)
        {
            delete s_bookie_sender;
            s_bookie_sender = nullptr;
            TTLOG(ERROR,13)<<"Clean up: bookie_sender;"<<endl;
        }
        if (init_flags & init_algo_sender)
        {
            delete s_algo_sender;
            s_algo_sender = nullptr;
            TTLOG(ERROR,13)<<"Clean up: s_algo_sender"<<endl;
        }
        if (init_flags & init_user_group_sender)
        {
            delete s_user_group_sender;
            s_user_group_sender = nullptr;
            TTLOG(ERROR,13)<<"Clean up: user_group_sender;"<<endl;
        }
        s_lbm_transaction_logger = nullptr;
        TTLOG(ERROR,13)<<"Clean up: lbm_lbm_transaction_logger;"<<endl;
        if (init_flags & init_lbm_context)
        {
            delete s_lbm_context;
            s_lbm_context = nullptr;
            TTLOG(ERROR,13)<<"Clean up: lbm_context;"<<endl;
        }
        return algoserver__error_comm;
    }
}

enum lbm_init_flags
InitLbmObjects (
                 const char* restrict app_name,
                 const char* restrict data_center,
                 const char* restrict algoserver__instance_identifier_name,
                 const char* restrict inst_id,
                 const char* restrict environment
                )
{
    enum lbm_init_flags         init_flags = (enum lbm_init_flags)0;
    char                        config_file[PATH_MAX];
    char                        topic_name[ALGOUTIL__MAX_LBM_TOPIC_STRING_SIZE];
    char                        bookie_topic_name[ALGOUTIL__MAX_LBM_TOPIC_STRING_SIZE];
    char                        algo_sender_topic_name[ALGOUTIL__MAX_LBM_TOPIC_STRING_SIZE];
    char                        triage_action_topic_name[ALGOUTIL__MAX_LBM_TOPIC_STRING_SIZE];
    char                        triage_response_topic_name[ALGOUTIL__MAX_LBM_TOPIC_STRING_SIZE];
    enum algoutil__error_code   algoutil_error;

    snprintf(config_file, PATH_MAX, "%s/" ALGOUTIL__LBM_CONFIG_FILE, algoserver__config_path);

    TTLOG(INFO,13)<<"Creating LBM Context config_file="<<config_file<<" app_name="<<app_name<<endl;

    try
    {
        lbm::LoadXmlConfig(config_file, app_name);
        lbm::ContextAttributes contextAttr;
        /*
        contextAttr.SetString("operational_mode", "sequential");
        if (!contextName.empty())
        {
            contextAttr.SetString("context_name", contextName.c_str());
        }
        */
        s_lbm_context = new lbm::Context(contextAttr);
    }
    catch (const lbm::LbmError& ex)
    {
        return init_flags;
    }
    init_flags = (enum lbm_init_flags)(init_flags | init_lbm_context);

    snprintf(topic_name,
        ALGOUTIL__MAX_LBM_TOPIC_STRING_SIZE,
        ALGO_TOPIC_STRING,
        algoserver__instance_identifier_name);

    TTLOG(INFO,13)<<"Creating receiver topic="<<topic_name<<endl;

    {
        lbm::Receiver::Callback cbk = [&](const lbm::Message& msg) {DataRecvd(msg); };
        try
        {
            lbm::ReceiverTopic topic(*s_lbm_context, topic_name);
            s_recver = new lbm::Receiver(*s_lbm_context, topic, cbk);
        }
        catch (const lbm::LbmError& ex)
        {
            return init_flags;
        }
        init_flags = (enum lbm_init_flags)(init_flags | init_algo_receiver);
    }

    AlgoUtil_MakeAlgoSideChannelInboundTopicName(algoserver__instance_identifier_name, topic_name);

    TTLOG(INFO,13)<<"Creating side channel receiver topic="<<topic_name<<endl;

    {
        lbm::Receiver::Callback cbk = [&](const lbm::Message& msg) {DataRecvd(msg); };
        try
        {
            lbm::ReceiverTopic topic(*s_lbm_context, topic_name);
            s_side_channel_recver = new lbm::Receiver(*s_lbm_context, topic, cbk);
        }
        catch (const lbm::LbmError& ex)
        {
            return init_flags;
        }
        init_flags = (enum lbm_init_flags)(init_flags | init_algo_side_channel_receiver);
    }

    AlgoUtil_MakeAlgoSideChannelOutboundTopicName(algoserver__instance_identifier_name, topic_name);

    TTLOG(INFO,13)<<"Creating side channel sender topic="<<topic_name<<endl;

    {
        try
        {
            lbm::SourceTopic topic(*s_lbm_context, topic_name);
            s_side_channel_sender = new lbm::Source(*s_lbm_context, topic);
        }
        catch (const lbm::LbmError& ex)
        {
            return init_flags;
        }
        init_flags = (enum lbm_init_flags)(init_flags | init_algo_side_channel_sender);
    }

    snprintf(topic_name,
        ALGOUTIL__MAX_LBM_TOPIC_STRING_SIZE,
        EDGESERVER_TOPIC_STRING);

    TTLOG(INFO,13)<<"Creating receiver EdgeServer topic="<<topic_name<<endl;

    {
        lbm::Receiver::Callback cbk = [&](const lbm::Message& msg) {DataRecvd(msg); };
        try
        {
            lbm::ReceiverTopic topic(*s_lbm_context, topic_name);
            s_edge_recver = new lbm::Receiver(*s_lbm_context, topic, cbk);
        }
        catch (const lbm::LbmError& ex)
        {
            return init_flags;
        }
        init_flags = (enum lbm_init_flags)(init_flags | init_edge_receiver);
    }

    snprintf(bookie_topic_name,
        ALGOUTIL__MAX_LBM_TOPIC_STRING_SIZE,
        BOOKIE_TOPIC_STRING,
        data_center);

    TTLOG(INFO,13)<<"Creating sender Bookie topic="<<bookie_topic_name<<endl;

    {
        try
        {
            lbm::SourceTopic topic(*s_lbm_context, bookie_topic_name);
            s_bookie_sender = new lbm::Source(*s_lbm_context, topic);
        }
        catch (const lbm::LbmError& ex)
        {
            return init_flags;
        }
        init_flags = (enum lbm_init_flags)(init_flags | init_bookie_sender);
    }

    {
        s_use_lbm_transaction_logger = s_algoConfig->Get<bool>("use_lbm_transaction_logger", false);
        TTLOG(INFO,13)<<"ConfigXml: use_lbm_transaction_logger="<<s_use_lbm_transaction_logger<<endl;
        if (s_use_lbm_transaction_logger)
        {
            const std::string dir_path = std::string("/var/lib/") + algoserver__component_name + "/recovery";
            unsigned int ump_flight_size = s_algoConfig->Get<unsigned int>("ump_flight_size", 1000);
            TTLOG(INFO,13)<<"Creating LBM Transaction Logger dir_path="<<dir_path<<" ump_flight_size="<<ump_flight_size<<endl;
            s_lbm_transaction_logger = std::make_shared<lbm::TransactionLogger>(dir_path,
                tt::messaging::order::enums::TT_MARKET_ID_ALGO_INSTRUMENT,
                ump_flight_size);
        }
    }
        
    snprintf(algo_sender_topic_name,
        sizeof(algo_sender_topic_name),
        ALGO_SENDER_TOPIC_STRING,
        algoserver__instance_identifier_name);

    {
        std::string  ump_ensemble = s_algoConfig->Get<std::string>("lbm_ump_ensemble", "");
        TTLOG(INFO,13)<<"ConfigXml: ump_ensemble="<<ump_ensemble<<endl;
        lbm_context_t* lbm_context = static_cast<lbm_context_t*>(*s_lbm_context);
        if (ump_ensemble.size() > 0)
        {
            s_use_ump = true;
            TTLOG(INFO,13)<<"Creating UMP sender topic="<<algo_sender_topic_name<<endl;
            s_algo_sender = AlgoUtil_CreateUMPSender(
                algo_sender_topic_name,
                ump_ensemble.c_str());
            if (ALGOUTIL__HINT_UNLIKELY(s_algo_sender == nullptr))
            {
                TTLOG(ERROR,13)<<"Failed to create sender topic="<<algo_sender_topic_name<<endl;
                return init_flags;
            }
            else
            {
                //This is useless, but keep it for the good structure.
                init_flags = (enum lbm_init_flags)(init_flags | init_algo_sender);
            }
        }
        else
        {
            s_use_ump = false;
            if (s_use_lbm_transaction_logger)
            {
                TTLOG(WARNING,13)<<"ConfigXml: inconsistent config. s_use_ump=false but use_lbm_transaction_logger=true. Setting use_lbm_transaction_logger=false"<<endl;
                s_use_lbm_transaction_logger = false;
            }

            TTLOG(INFO,13)<<"Creating LBM sender topic="<<algo_sender_topic_name<<endl;
            try
            {
                lbm::SourceTopic topic(*s_lbm_context, algo_sender_topic_name);
                s_algo_sender = new lbm::Source(*s_lbm_context, topic);
                //This is useless, but keep it for the good structure.
                init_flags = (enum lbm_init_flags)(init_flags | init_algo_sender);
            }
            catch (const lbm::LbmError& ex)
            {
                TTLOG(ERROR,13)<<"Failed to create sender topic="<<algo_sender_topic_name<<" error="<<ex.what()<<endl;
                return init_flags;
            }
        }
    }

    strcpy(topic_name, PATTERN_OC_TOPIC_STRING);

    TTLOG(INFO,13)<<"Creating receiver OC topic="<<topic_name<<endl;

    {
        lbm::Receiver::Callback cbk = [&](const lbm::Message& msg) {DataRecvd(msg); };
        try
        {
            s_oc_recver = new lbm::WildcardReceiver(*s_lbm_context, topic_name, cbk);
        }
        catch (const lbm::LbmError& ex)
        {
            return init_flags;
        }
        init_flags = (enum lbm_init_flags)(init_flags | init_oc_receiver);
    }

    TTLOG(INFO,13)<<"Creating source for User Group topic="<<USER_GROUP_TOPIC_STRING<<endl;

    {
        try
        {
            lbm::SourceTopic topic(*s_lbm_context, USER_GROUP_TOPIC_STRING);
            s_user_group_sender = new lbm::Source(*s_lbm_context, topic);
        }
        catch (const lbm::LbmError& ex)
        {
            return init_flags;
        }
        init_flags = (enum lbm_init_flags)(init_flags | init_user_group_sender);
    }

    std::string triage_env(environment);
    try
    {
        // TRIAGE.ACTION.[ENV]
        std::memset(triage_action_topic_name, 0, ALGOUTIL__MAX_LBM_TOPIC_STRING_SIZE);
        std::transform(triage_env.begin(), triage_env.end(), triage_env.begin(), toupper);
        AlgoUtil_MakeTriageActionTopicName(triage_env.c_str(), triage_action_topic_name);
        TTLOG(INFO,13)<<"Creating Triage Action Topic="<<triage_action_topic_name<<endl;
        lbm::ReceiverTopic topic(*s_lbm_context, triage_action_topic_name);
        s_triage_action_env_recver = new lbm::Receiver(*s_lbm_context, topic, DataRecvd);
    }
    catch (const lbm::LbmError& ex)
    {
        return init_flags;
    }
    init_flags = (enum lbm_init_flags)(init_flags | init_triage_action_env_receiver);

    std::string triage_env_app = triage_env + "." + std::string("ALGOSERVER");
    try
    {
        // TRIAGE.ACTION.[ENV].[APP]
        // NOTE: Argument "app_name" passed into this function is too specific.
        //       It distinguishes between "algoserver_exec" vs. "algoserver_debug"
        //       We need the generic app_name "ALGOSERVER" in uppercase!
        std::memset(triage_action_topic_name, 0, ALGOUTIL__MAX_LBM_TOPIC_STRING_SIZE);
        AlgoUtil_MakeTriageActionTopicName(triage_env_app.c_str(), triage_action_topic_name);
        TTLOG(INFO,13)<<"Creating Triage Action Topic="<<triage_action_topic_name<<endl;
        lbm::ReceiverTopic topic(*s_lbm_context, triage_action_topic_name);
        s_triage_action_env_app_recver = new lbm::Receiver(*s_lbm_context, topic, DataRecvd);
    }
    catch (const lbm::LbmError& ex)
    {
        return init_flags;
    }
    init_flags = (enum lbm_init_flags)(init_flags | init_triage_action_env_app_receiver);

    try
    {
        // TRIAGE.ACTION.[ENV].[APP].[inst_id]
        // NOTE: Unlike other segments, [inst_id] is NOT uppercase!
        std::memset(triage_action_topic_name, 0, ALGOUTIL__MAX_LBM_TOPIC_STRING_SIZE);
        std::string triage_env_app_inst = triage_env_app + "." + std::string(inst_id);
        AlgoUtil_MakeTriageActionTopicName(triage_env_app_inst.c_str(), triage_action_topic_name);
        TTLOG(INFO,13)<<"Creating Triage Action Topic="<<triage_action_topic_name<<endl;
        lbm::ReceiverTopic topic(*s_lbm_context, triage_action_topic_name);
        s_triage_action_env_app_inst_recver = new lbm::Receiver(*s_lbm_context, topic, DataRecvd);
    }
    catch (const lbm::LbmError& ex)
    {
        return init_flags;
    }
    init_flags = (enum lbm_init_flags)(init_flags | init_triage_action_env_app_inst_receiver);

    try
    {
        // TRIAGE.ACTION.[ENV].[APP].[DATACENTER]
        std::memset(triage_action_topic_name, 0, ALGOUTIL__MAX_LBM_TOPIC_STRING_SIZE);
        std::string triage_data_center(data_center);
        std::transform(triage_data_center.begin(), triage_data_center.end(), triage_data_center.begin(), toupper);
        std::string triage_env_app_dc = triage_env_app + "." + std::string(triage_data_center);
        AlgoUtil_MakeTriageActionTopicName(triage_env_app_dc.c_str(), triage_action_topic_name);
        TTLOG(INFO,13)<<"Creating Triage Action Topic="<<triage_action_topic_name<<endl;
        lbm::ReceiverTopic topic(*s_lbm_context, triage_action_topic_name);
        s_triage_action_env_app_dc_recver = new lbm::Receiver(*s_lbm_context, topic, DataRecvd);
    }
    catch (const lbm::LbmError& ex)
    {
        return init_flags;
    }
    init_flags = (enum lbm_init_flags)(init_flags | init_triage_action_env_app_dc_receiver);

    return init_all_succeeds;
}

enum algoserver__error_code
    AlgoServer_ShutdownComm (void)
{
    TTLOG(INFO,13)<<"AlgoServer_ShutdownComm Started"<<endl;

    s_algo_transfer_requests.clear();
    s_bookie_download_request = nullptr;

    for (auto& pair : s_algo_senders)
    {
        lbm::ChannelSource* channel_source = pair.second;
        delete channel_source;
    }
    s_algo_senders.clear();

    for (auto& pair : s_risk_senders)
    {
        for (auto& inner_pair : pair.second.senders)
        {
            lbm::ChannelSource* channel_source = inner_pair.second;
            delete channel_source;
        }
        pair.second.senders.clear();
        lbm::Source* source = pair.second.source;
        delete source;
    }
    s_risk_senders.clear();

    for (auto& pair : s_user_group_senders)
    {
        lbm::ChannelSource* channel_source = pair.second;
        delete channel_source;
    }
    s_user_group_senders.clear();

    for (auto& pair : s_oc_ordertag_senders)
    {
        lbm::Source* source = pair.second;
        delete source;
    }
    s_oc_ordertag_senders.clear();

    delete s_user_group_sender;
    s_user_group_sender = nullptr;
    delete s_oc_recver;
    s_oc_recver = nullptr;
    delete s_edge_recver;
    s_edge_recver = nullptr;
    delete s_side_channel_recver;
    s_side_channel_recver = nullptr;
    delete s_side_channel_sender;
    s_side_channel_sender = nullptr;
    delete s_algo_sender;
    s_algo_sender = nullptr;
    s_lbm_transaction_logger = nullptr;
    delete s_bookie_sender;
    s_bookie_sender = nullptr;
    delete s_recver;
    s_recver = nullptr;
    delete s_triage_action_env_recver;
    s_triage_action_env_recver = nullptr;
    delete s_triage_action_env_app_recver;
    s_triage_action_env_app_recver = nullptr;
    delete s_triage_action_env_app_inst_recver;
    s_triage_action_env_app_inst_recver = nullptr;
    delete s_triage_action_env_app_dc_recver;
    s_triage_action_env_app_dc_recver = nullptr;
    delete s_lbm_context;
    s_lbm_context = nullptr;

    TTLOG(INFO,13)<<"AlgoServer_ShutdownComm Ended"<<endl;
    return algoserver__error_none;
}

void
AlgoServer_SendAlertExecutionReport (
                                     algojob__alert*        alert_data,
                                     ttsdk_params_internal* algo_params,
                                     uuid_t*                algo_order_id,
                                     unsigned long long     user_id,
                                     unsigned long long     algo_instr_id,
                                     unsigned long long     connection_id,
                                     unsigned int           algo_market_id,
                                     unsigned long long     response_count
                                    )
{
    char                            encoded_data[ALGOSERVER__MAX_PROTOCOL_ENCODING_SIZE];
    size_t                          encoded_size;
    AlgoServer_EncodeAlertExecutionReport(
                                          alert_data,
                                          algo_params,
                                          algo_order_id,
                                          user_id,
                                          algo_instr_id,
                                          connection_id,
                                          algo_market_id,
                                          response_count,
                                          encoded_data,
                                          &encoded_size
                                         );
    auto channel_sender = GetOrCreateAlgoChannelSender(connection_id);
    if (channel_sender == nullptr)
    {
        TTLOG(ERROR,13)<<"Error no algo channel sender for account_id="<<connection_id<<"lu"<<endl;
    }
    else
    {
        try
        {
            const uint64_t timestamp = AlgoUtil_CurrentTimeNS();
            ChannelSenderSend(channel_sender, timestamp, encoded_data, encoded_size, LBM_SRC_BLOCK);
        }
        catch (const lbm::LbmError& ex)
        {
            TTLOG(WARNING,13)<<"AlgoServer_SendAlertExecutionReport: lbm_error="<<ex.what()<<" use_lbm_transaction_logger="<<s_use_lbm_transaction_logger<<endl;
        }
    }
}

inline void
AlgoServer_SendSideChannelMessage (
    unsigned long long account_id,
    const char* encoded_data,
    size_t encoded_size,
    const std::string& logInfo
)
{
    auto channel_sender = GetOrCreateAlgoSideChannelSender(account_id);

    if (channel_sender == nullptr)
    {
        TTLOG(ERROR,13)<<""<<logInfo<<": Cannot create algo sidechannel sender for account_id="<<account_id<<"lu"<<endl;
        try
        {
            s_side_channel_sender->Send(encoded_data, encoded_size, LBM_SRC_BLOCK);
        }
        catch (const lbm::LbmError& ex)
        {
            TTLOG(WARNING,13)<<""<<logInfo<<": lbm_error="<<ex.what()<<" without channel="<<account_id<<"lu"<<endl;
        }
    }
    else
    {
        try
        {
            channel_sender->Send(encoded_data, encoded_size, LBM_SRC_BLOCK);
        }
        catch (const lbm::LbmError& ex)
        {
            TTLOG(WARNING,13)<<""<<logInfo<<": lbm_error="<<ex.what()<<" channel="<<account_id<<endl;
        }
    }
}

void
AlgoServer_SendAlert (
                      std::list<algojob__alert*>*   audit_trail_alerts,
                      unsigned long long            connection_id, //account_id
                      unsigned long long            algo_instr_id,
                      struct algojob__alerts*       alerts
                     )
{
    char                            encoded_data[ALGOSERVER__MAX_PROTOCOL_ENCODING_SIZE];
    size_t                          encoded_size;
    enum algoserver__error_code     algoserver_error
        = AlgoServer_EncodeAlert(
                                 algo_instr_id,
                                 alerts,
                                 audit_trail_alerts,
                                 encoded_data,
                                 &encoded_size,
                                connection_id
                                );
    if(ALGOUTIL__HINT_UNLIKELY(algoserver_error != algoserver__error_none))
    {
        TTLOG(WARNING,13)<<"            AlgoServer_SendAlert: Encode failed: "<<ALGOSERVER_ERROR_STR[algoserver_error]<<endl;
        return;
    }

    AlgoServer_SendSideChannelMessage(connection_id, encoded_data, encoded_size, "AlgoServer_SendAlert");
}

void
AlgoServer_SendExportValue (
                            unsigned long long              user_id,
                            unsigned long long              algo_instr_id,
                            unsigned long long              connection_id, //account_id
                            struct algojob__export_values*  export_values
                           )
{
    char                            encoded_data[ALGOSERVER__MAX_PROTOCOL_ENCODING_SIZE];
    size_t                          encoded_size;
    enum algoserver__error_code     algoserver_error
        = AlgoServer_EncodeExportValue(
                                       user_id,
                                       algo_instr_id,
                                       export_values,
                                       encoded_data,
                                       &encoded_size,
                                       connection_id
                                      );
    if(ALGOUTIL__HINT_UNLIKELY(algoserver_error != algoserver__error_none))
    {
        TTLOG(WARNING,13)<<"SendExportValue: Encode failed: "<<ALGOSERVER_ERROR_STR[algoserver_error]<<" account_id="<<connection_id<<"lu"<<endl;
        return;
    }
    AlgoServer_SendSideChannelMessage(connection_id, encoded_data, encoded_size, "SendExportValue");
}

void
AlgoServer_SendResponse (algoserver__response* response, const bool isFakeExecutionReport)
{
    char encoded_response[ALGOSERVER__MAX_PROTOCOL_ENCODING_SIZE];
    size_t encoded_size;

    algoserver__error_code algoserver_error;

    switch(response->id)
    {
    case algojob__response_request_failure:
    case algojob__response_request_purge:
    {
        auto& request_failure = response->data.request_failure;

        algoserver_error = AlgoServer_EncodeFailure(response->user_id,
                                                    response->connection_id,
                                                    &response->inst_id,
                                                    response->instr_id,
                                                    response->market_id,
                                                    request_failure.response_count,
                                                    request_failure.request_id,
                                                    request_failure.request_flags,
                                                    response->user_request_id,
                                                    request_failure.state,
                                                    request_failure.failure,
                                                    &response->params,
                                                    encoded_response,
                                                    &encoded_size);

        break;
    }

    case algojob__response_request_complete:
    {
        auto& request_complete = response->data.request_complete;

        algoserver_error = AlgoServer_EncodeCompletion(response->user_id,
                                                       response->connection_id,
                                                       &response->inst_id,
                                                       response->instr_id,
                                                       response->market_id,
                                                       request_complete.response_count,
                                                       request_complete.request_id,
                                                       request_complete.request_flags,
                                                       response->user_request_id,
                                                       request_complete.state,
                                                       &response->params,
                                                       encoded_response,
                                                       &encoded_size, 
                                                       algojob__request_failure_none,
                                                       isFakeExecutionReport);

        if (algoserver_error == algoserver__error_none)
        {
            AlgoServer_MaybeSendToUserGroup(response, encoded_response, encoded_size);
        }

        break;
    }

    case algojob__response_status:
    {
        auto& status = response->data.status;

        algoserver_error = AlgoServer_EncodeStatus(response->user_id,
                                                   response->connection_id,
                                                   &response->inst_id,
                                                   response->instr_id,
                                                   response->market_id,
                                                   status.response_count,
                                                   status.state,
                                                   &response->params,
                                                   encoded_response,
                                                   &encoded_size);

        break;
    }

    case algojob__response_value:
        algoserver_error = AlgoServer_EncodeValue(
                                                  &response->inst_id,
                                                  response->connection_id,
                                                  response->instr_id,
                                                  &response->data.value,
                                                  &response->params,
                                                  encoded_response,
                                                  &encoded_size
                                                 );

        break;


    case algojob__response_register_side_channel:
    case algojob__response_unregister_side_channel:
    case algojob__response_export_value_snapshot:
    case algojob__response_cli_cmd:
        algoserver_error = AlgoServer_EncodeSideChannelResponse(response, encoded_response, &encoded_size);
        break;

    case algojob__response_init:
    case algojob__response_alert:           //should not come here
    case algojob__response_export_value:    //should not come here
    case algojob__response_reconn_snapshot:
    case algojob__response_reconn_finished:
    case algojob__response_loopback_order_request: 
        algoserver_error = algoserver__error_protocol;
        break;
    }

    if(ALGOUTIL__HINT_UNLIKELY(algoserver_error != algoserver__error_none))
    {
        TTLOG(ERROR,13)<<"            Encode failed for response="<<ALGO_RESPONSE_ID_STR[response->id]<<". algoserver_error="<<ALGOSERVER_ERROR_STR[algoserver_error]<<endl;
        free(response);
        return;
    }

    int lbm_error = 0;
    if (   response->id == algojob__response_export_value
        || response->id == algojob__response_register_side_channel
        || response->id == algojob__response_unregister_side_channel
        || response->id == algojob__response_export_value_snapshot
        || response->id == algojob__response_cli_cmd
       )
    {
        AlgoServer_SendSideChannelMessage(response->connection_id, encoded_response, encoded_size, "SendResponse");
    }
    else
    {
        auto channel_sender =  GetOrCreateAlgoChannelSender(response->connection_id);
        if (channel_sender == nullptr)
        {
            TTLOG(ERROR,13)<<"Error no algo channel sender for account_id="<<response->connection_id<<"lu"<<endl;
        }
        else if (encoded_size == 0)
        {
            TTLOG(ERROR,13)<<"Skip lbm send, encoded_size=0"<<endl;
        }
        else
        {
            try
            {
                const uint64_t timestamp = AlgoUtil_CurrentTimeNS();
                ChannelSenderSend(channel_sender, timestamp, encoded_response, encoded_size, LBM_SRC_BLOCK);
            }
            catch (const lbm::LbmError& ex)
            {
                TTLOG(WARNING,13)<<"Error sending response: : lbm_error="<<ex.what()<<" use_lbm_transaction_logger"<<endl;
            }
        }
    }
    free(response);
}

static enum algoserver__error_code
SendBookieRequest (const char* data, size_t len, lbm::RequestCallback cbk, std::shared_ptr<lbm::Request>& request)
{
    try
    {
        request = s_bookie_sender->SendRequest(data, len, cbk);
    }
    catch (const lbm::LbmError& ex)
    {
        TTLOG(WARNING,13)<<"Error sending request, lbm_error="<<ex.what()<<endl;
        return algoserver__error_comm;
    }

    return algoserver__error_none;
}

void
TransferAlgoFrom (std::string algoserver_id)
{
    char   request_buffer[ALGOSERVER__MAX_PROTOCOL_ENCODING_SIZE];
    size_t buf_size;

    AlgoServer_EncodeBookieDownloadRequest(request_buffer, &buf_size, algoserver_id.c_str(), false);

    TTLOG(INFO,13)<<"TransferAlgo: sending book download request for algoserver_id="<<algoserver_id<<endl;

    std::shared_ptr<lbm::Request> request;
    SendBookieRequest(
                      request_buffer,
                      buf_size,
                      &TransferAlgoRequestCallback,
                      request
                    );
    //  Not clear how to remove the request object from this collection.
    //  Does the response (handled below in TransferAlgoRequestCallback) have the same algoserver_id?
    //  Hopefully, there aren't too many of these request so it's OK to let them stick around
    s_algo_transfer_requests[algoserver_id] = request;
}
 
static int
TransferAlgoRequestCallback (const lbm::Message& message)
{
    TTLOG(INFO,13)<<"TransferAlgo response received on LBM thread"<<endl;
    AlgoServer_DecodeAndForwardBookieDownload(message.Data(), message.Length());
    return 0;
}

static int
DownloadBookRequestCallback (const lbm::Message& message)
{
    pthread_mutex_lock(&book_dl_lock);

    if(book_dl_status == algoserver__book_dl_complete)
    {
        TTLOG(INFO,13)<<"Duplicate, book download response received, ignoring"<<endl;
    }
    else
    {
        book_dl_status = algoserver__book_dl_complete;

        TTLOG(INFO,13)<<"Book download response received"<<endl;

        sem_post(&book_dl_semaphore);

        AlgoServer_DecodeAndForwardBookieDownload(message.Data(), message.Length());
    }

    pthread_mutex_unlock(&book_dl_lock);

    return 0;
}

void
AlgoServer_RequestBookDownload ()
{
    char   request_buffer[ALGOSERVER__MAX_PROTOCOL_ENCODING_SIZE];
    size_t buf_size;

    AlgoServer_EncodeBookieDownloadRequest(request_buffer, &buf_size, algoserver__instance_identifier_name, true);

    TTLOG(INFO,13)<<"Sending book download request"<<endl;

    std::shared_ptr<lbm::Request> request;
    SendBookieRequest(
                      request_buffer,
                      buf_size,
                      &DownloadBookRequestCallback,
                      request
                    );
    s_bookie_download_request = request;
}

static int
DownloadOrderTagCallback (const lbm::Message& message)
{
    TTLOG(INFO,13)<<"DownloadOrderTagCallback: OTD ER received on LBM Thread!"<<endl;
    uuid_t algo_inst_id;
    AlgoServer_DecodeAndForwardOrderTagDownload(message.Data(), message.Length(), &algo_inst_id);

    if (!uuid_is_null(algo_inst_id))
    {
        tt::algoutil::ttuuid algo_inst_ttuuid(algo_inst_id);
        TTLOG(INFO,13)<<"DownloadOrderTagCallback: Forwarded Order Tags to Server Main Thread: "<<endl;
    }

    return 0;
}

void
AlgoServer_CreateOrderTagSendersFromConnectionsMap (std::unordered_map<unsigned long long, unsigned int> connectionsMap)
{
    for (const auto& pair : connectionsMap)
    {
        GetOrCreateOrderTagSender(pair.second);
    }
}

bool
AlgoServer_SendOrderTagNewOrderSingle (ttsdk_params_internal& params)
{
    // ord_id on an incoming Algo NOS is the algo_inst_id.
    if(!TTSDK_PARAM_IS_SET(&params, ord_id))
    {
        TTLOG(WARNING,13)<<"AlgoServer_SendOrderTagNewOrderSingle: Error sending OTD NOS: algo_inst_id is missing but OTD error is not fatal: "<<endl;
        return false;
    }
    tt::algoutil::ttuuid algo_inst_id(params.ord_id);

    // Encode OrderTag NOS.
    char   data[ALGOSERVER__MAX_PROTOCOL_ENCODING_SIZE];
    size_t size;
    AlgoServer_EncodeOrderTagNewOrderSingle(&params, data, &size);

    // Get the Order Tag Sender using the appropriate topic.
    // market_id on an incoming Algo NOS sent from TTW is always TT_MARKET_ID_ALGO_INSTRUMENT.
    // That's not the market that we should use for the OTD NOS topic.
    // Instead, use the primary_market_id of the Algo NOS which identifies a real native exchange.
    if(!TTSDK_PARAM_IS_SET(&params, prim_mkt))
    {
        TTLOG(WARNING,13)<<"AlgoServer_SendOrderTagNewOrderSingle: Error sending OTD NOS: Unable to find primary_market_id but OTD error is not fatal: "<<endl;
        return false;
    }
    unsigned int market_id = static_cast<unsigned int>(params.prim_mkt);
    auto sender = GetOrCreateOrderTagSender(market_id);
    if (sender == nullptr)
    {
        TTLOG(WARNING,13)<<"AlgoServer_SendOrderTagNewOrderSingle: Error sending OTD NOS: Unable to find sender but OTD error is not fatal: "<<endl;
        return false;
    }

    // We got the Order Tag Sender with the proper topic and we are ready to send.
    TTLOG(INFO,13)<<"AlgoServer_SendOrderTagNewOrderSingle: Sending OTD NOS: "<<endl;
    std::shared_ptr<lbm::Request> requestPtr = nullptr;
    try
    {
        requestPtr = sender->SendRequest(data, size, &DownloadOrderTagCallback);
    }
    catch (const lbm::LbmError& ex)
    {
        TTLOG(WARNING,13)<<"AlgoServer_SendOrderTagNewOrderSingle: Error sending OTD NOS: LBM send failed but OTD error is not fatal: "<<endl;
        return false;
    }
    if (requestPtr == nullptr)
    {
        TTLOG(WARNING,13)<<"AlgoServer_SendOrderTagNewOrderSingle: Error sending OTD NOS: LBM Request Ptr is null but OTD error is not fatal: "<<endl;
        return false;
    }

    // OTD NOS sent successfully.
    // We need to save the requestPtr because the underlying LBM library can
    // prematurely dispose of the request object if its ref count drops to zero.
    AlgoServer_EmplaceOrderTagRequestPtr(algo_inst_id, requestPtr);
    TTLOG(INFO,13)<<"AlgoServer_SendOrderTagNewOrderSingle: Sent OTD NOS: "<<endl;
    return true;
}

void
AlgoServer_EmplaceOrderTagRequestPtr(const tt::algoutil::ttuuid& algo_inst_id,
                                     const std::shared_ptr<lbm::Request>& requestPtr)
{
    s_ordertag_requests.emplace(algo_inst_id, requestPtr);
    TTLOG(INFO,13)<<"AlgoServer_EmplaceOrderTagRequestPtr: Emplaced ptr: "<<endl;
}

void
AlgoServer_EraseOrderTagRequestPtr(const tt::algoutil::ttuuid& algo_inst_id)
{
    s_ordertag_requests.erase(algo_inst_id);
    TTLOG(INFO,13)<<"AlgoServer_EraseOrderTagRequestPtr: Erased ptr: "<<endl;
}

void
AlgoServer_SendChildCancel (const ttsdk_params_internal& params)
{
    char   data[ALGOSERVER__MAX_PROTOCOL_ENCODING_SIZE];
    size_t size;

    AlgoServer_EncodeChildCancel(&params, data, &size);
    unsigned int market_id = static_cast<unsigned int>(params.mkt_id);

    auto sender = AlgoServer_GetOrCreateChildSender(market_id, params.account);

    if (sender == nullptr)
    {
        tt::algoutil::ttuuid order_id(params.ord_id);
        TTLOG(ERROR,13)<<"Error sending child cancel: Unable to find sender."<<endl;

        return;
    }

    TTLOG(INFO,13)<<"Sending child cancel on market_id="<<market_id<<" account_id="<<params.account<<"lu"<<endl;

    try
    {
        sender->Send(data, size, LBM_SRC_BLOCK);
    }
    catch (const lbm::LbmError& ex)
    {
        tt::algoutil::ttuuid order_id(params.ord_id);
        TTLOG(ERROR,13)<<"Error sending child cancel: LBM send failed."<<endl;

        return;
    }

    // TODO properly clean up if no algos exist for a given account
    //delete sender;
}


lbm::ChannelSource*
GetOrCreateAlgoChannelSender (unsigned long long account_id)
{
    auto found = s_algo_senders.find(account_id);
    if (found == s_algo_senders.end())
    {
        lbm::ChannelSource* channel_source = nullptr;
        try
        {
            if (s_use_lbm_transaction_logger)
            {
                //  It's safe to use reinterpret_cast because we do instantiate SourceTL if s_use_lbm_transaction_logger=true 
                lbm::SourceTL* sourceTL = reinterpret_cast<lbm::SourceTL*> (s_algo_sender);
                channel_source = new lbm::ChannelSourceTL(*sourceTL, account_id);
            }
            else
            {
                channel_source = new lbm::ChannelSource(*s_algo_sender, account_id);
            }
        }
        catch (const lbm::LbmError& ex)
        {
            TTLOG(ERROR,13)<<"Error creating algo sender. account_id="<<account_id<<"lu error="<<ALGOUTIL_ERROR_STR[algoutil__error_create_lbm_channel_source]<<" "<<ex.what()<<" use_lbm_transaction_logger="<<s_use_lbm_transaction_logger<<endl;
            return nullptr;
        }
        TTLOG(INFO,13)<<"Created algo channel sender. account_id="<<account_id<<"lu use_lbm_transaction_logger="<<s_use_lbm_transaction_logger<<endl;
        return s_algo_senders.emplace(account_id, channel_source).first->second;
    }
    else
    {
        return found->second;
    }
}

static lbm::ChannelSource*
GetOrCreateAlgoSideChannelSender (unsigned long long account_id)
{
    auto found = s_algo_sidechannel_sprectrum_senders.find(account_id);
    if (found == s_algo_sidechannel_sprectrum_senders.end())
    {
        lbm::ChannelSource* channel_source = nullptr;
        try
        {
            channel_source = new lbm::ChannelSource(*s_side_channel_sender, account_id);
        }
        catch (const lbm::LbmError& ex)
        {
            TTLOG(ERROR,13)<<"Error creating algo sender. account_id="<<account_id<<"lu error="<<ALGOUTIL_ERROR_STR[algoutil__error_create_lbm_channel_source]<<" "<<ex.what()<<" useTransactionLogger="<<s_use_lbm_transaction_logger<<endl;
            return nullptr;
        }
        TTLOG(INFO,13)<<"Created algo channel sender. account_id="<<account_id<<"lu useTransactionLogger="<<s_use_lbm_transaction_logger<<endl;
        return s_algo_sidechannel_sprectrum_senders.emplace(account_id, channel_source).first->second;
    }
    else
    {
        return found->second;
    }
}


lbm::ChannelSource*
AlgoServer_GetOrCreateChildSender (unsigned int mkt_id, unsigned long long account_id)
{
    risk_channel_senders* channel_senders;
    auto found = s_risk_senders.find(mkt_id);
    if (found == s_risk_senders.end())
    {
        //  put a placeholder into the outer (by market ID) collection
        channel_senders = &s_risk_senders.emplace(mkt_id, risk_channel_senders()).first->second;

        //  Now populate the Source in that placeholder item
        char topic_name[ALGOUTIL__MAX_LBM_TOPIC_STRING_SIZE];
        snprintf(topic_name,
                 ALGOUTIL__MAX_LBM_TOPIC_STRING_SIZE,
                 RISK_TOPIC_STRING,
                 mkt_id);

        try
        {
            lbm::SourceTopic topic(*s_lbm_context, topic_name);
            channel_senders->source = new lbm::Source(*s_lbm_context, topic);
        }
        catch (const lbm::LbmError& ex)
        {
            TTLOG(ERROR,13)<<"Failed to create Risk topic="<<topic_name<<". error="<<ex.what()<<endl;
            return nullptr;
        }
    }
    else
    {
        channel_senders = &found->second;
    }

    auto found_sender = channel_senders->senders.find(account_id);
    if (found_sender != channel_senders->senders.end())
    {
        return found_sender->second;
    }

    lbm::ChannelSource* sender;
    try
    {
        sender = new lbm::ChannelSource(*channel_senders->source, account_id);
    }
    catch (const lbm::LbmError& ex)
    {
        TTLOG(ERROR,13)<<"Error creating child sender."<<endl;
        return nullptr;
    }

    TTLOG(INFO,13)<<"Created child cancel channel sender. account_id="<<account_id<<"lu market="<<mkt_id<<endl;
    return (channel_senders->senders.emplace(account_id, sender).first->second);
}


void
SimpleSend (char* encoded_response, size_t encoded_size)
{
    try
    {
        s_algo_sender->Send(encoded_response, encoded_size, LBM_SRC_BLOCK);
    }
    catch (const lbm::LbmError& ex)
    {
        TTLOG(WARNING,13)<<"Error sending response: Lbm send failed. lbm_error="<<ex.what()<<endl;
    }
}

lbm::ChannelSource*
GetOrCreateUserGroupChannelSender (unsigned long long user_group_id)
{
    auto iter = s_user_group_senders.find(user_group_id);
    if ( iter != s_user_group_senders.end())
    {
        return iter->second;
    }
    try
    {
        lbm::ChannelSource* sender = new lbm::ChannelSource(*s_user_group_sender, user_group_id);
        return s_user_group_senders.emplace(user_group_id, sender).first->second;
    }
    catch (const lbm::LbmError& ex)
    {
        return nullptr;
    }
}

static lbm::Source*
GetOrCreateOrderTagSender (unsigned long long market_id)
{
    // See if sender already exists for the given market_id.
    auto iter = s_oc_ordertag_senders.find(market_id);
    if (iter != s_oc_ordertag_senders.end())
    {
        // Sender exists!
        return iter->second;
    }

    // Sender doesn't exist yet - let's create one.
    char oc_ordertag_sender_topic_name[ALGOUTIL__MAX_LBM_TOPIC_STRING_SIZE];
    snprintf(oc_ordertag_sender_topic_name,
             ALGOUTIL__MAX_LBM_TOPIC_STRING_SIZE,
             OC_ORDERTAG_TOPIC_STRING,
             market_id);
    TTLOG(INFO,13)<<"Creating sender OC Ordertag topic="<<oc_ordertag_sender_topic_name<<endl;
    try
    {
        lbm::SourceTopic topic(*s_lbm_context, oc_ordertag_sender_topic_name);
        auto oc_ordertag_sender = new lbm::Source(*s_lbm_context, topic);
        return s_oc_ordertag_senders.emplace(market_id, oc_ordertag_sender).first->second;
    }
    catch (const lbm::LbmError& ex)
    {
        return nullptr;
    }
}

void
AlgoServer_MaybeSendToUserGroup (algoserver__response* response,
                                 char*                 encoded_response,
                                 size_t                encoded_size)
{
    if (response->data.request_complete.request_id == algojob__request_order_pass)
    {
        enum order_pass obp_state = order_pass_none;
        if (TTSDKInt_GetOrderPassingState(CastIntParamsToExt(&response->params), &obp_state) == ttsdk_error_none && obp_state == order_pass_init)
        {
            unsigned long long pass_to_group_id = 0;
            if (TTSDKInt_GetPassToGroupId(CastIntParamsToExt(&response->params), &pass_to_group_id) == ttsdk_error_none)
            {
                SendToUserGroupChannel(encoded_response,
                                       encoded_size,
                                       pass_to_group_id);
            }
        }
    }
}

void
SendToUserGroupChannel (char* encoded_response, size_t encoded_size, unsigned long long user_group_id)
{
    auto channel_sender = GetOrCreateUserGroupChannelSender(user_group_id);
    if (!channel_sender)
    {
        TTLOG(ERROR,13)<<"No Channel sender for user_group_id="<<user_group_id<<"lu"<<endl;
        return;
    }
    try
    {
        s_user_group_sender->Send(encoded_response, encoded_size, LBM_SRC_BLOCK);
        TTLOG(INFO,13)<<"Send to "<<USER_GROUP_TOPIC_STRING<<" topic with channel sender for user_group_id="<<user_group_id<<"lu"<<endl;
    }
    catch (const lbm::LbmError& ex)
    {
        TTLOG(ERROR,13)<<"Failed to send through channel sender for user_group_id="<<user_group_id<<"lu"<<endl;
    }
}

bool
GetLbmSenderMode ()
{
    return s_use_ump;
}

bool
SetLbmSenderMode (const bool useUMP)
{
    if (!s_use_lbm_transaction_logger)
    {
        //  can't switch the mode unless we are using Transaction Logger
        TTLOG(ERROR,13)<<"Cannot change the LbmSenderMode because use_lbm_transaction_logger="<<s_use_lbm_transaction_logger<<endl;
        return false;
    }
    TTLOG(INFO,13)<<"SetLbmSenderMode to "<<useUMP?"UMP":"LBM"<<endl;
    //  copied from ase/native_spreader/source/NativeAseServer.h
    std::promise<std::string> result;
    std::future<std::string> result_future = result.get_future();
    s_lbm_transaction_logger->AsyncSwitchSourceUmpMode(useUMP,
        [&result](const std::string& status) mutable
        {
            result.set_value(status);
        });
    std::string newState = result_future.get();
    TTLOG(INFO,13)<<"SetLbmSenderMode to "<<useUMP?"UMP":"LBM"<<" "<<newState<<endl;
    return true;
}

//  may throw 
void
ChannelSenderSend (lbm::ChannelSource* channel_sender, const uint64_t timestamp, const char* encoded_data, const size_t encoded_size, const int flags)
{
    if (s_use_lbm_transaction_logger)
    {
        //  It's safe to use reinterpret_cast because we do instantiate ChannelSourceTL if s_use_lbm_transaction_logger=true 
        lbm::ChannelSourceTL* channel_senderTL = reinterpret_cast<lbm::ChannelSourceTL*>(channel_sender);
        channel_senderTL->SendTL(timestamp, encoded_data, encoded_size, flags);
    }
    else
    {
        channel_sender->Send(encoded_data, encoded_size, flags);
    }
}
