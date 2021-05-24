        SERVER_ELOG("Cannot change the LbmSenderMode because use_lbm_transaction_logger=%d", s_use_lbm_transaction_logger);

        SERVER_ILOG("SetLbmSenderMode to %s", useUMP ?"UMP":"LBM");

        SERVER_ELOG("Error creating child sender."
            " market_id=%u"
            " account_id=%llu"
            " error=%s",
            mkt_id,
            account_id,
            std::string(ex.what()));

    SERVER_ILOG("AlgoServer_EmplaceOrderTagRequestPtr: Emplaced ptr: "
                "algo_inst_id=%s "
                "s_ordertag_requests.size()=%d",
                algo_inst_id.to_string(),
                s_ordertag_requests.size());

        SERVER_ILOG("AlgoServer_SendOrderTagNewOrderSingle: Sent OTD NOS: "
                "algo_inst_id=%s",
                algo_inst_id.to_string());

        SERVER_WLOG("AlgoServer_SendOrderTagNewOrderSingle: Error sending OTD NOS: LBM Request Ptr is null but OTD error is not fatal: "
                    "algo_inst_id=%s",
                    algo_inst_id.to_string());

        SERVER_WLOG("SendExportValue: Encode failed: %s account_id=%llu", ALGOSERVER_ERROR_STR[algoserver_error], connection_id);

    SERVER_ILOG("using the following string %s to generate session id: 0x%016llX",
        std::string(topic_name),
        session_id);

        SERVER_ILOG("AlgoServer_EmplaceOrderTagRequestPtr: Emplaced ptr: "
                "algo_inst_id=%s "
                "s_ordertag_requests.size()=%d",
                algo_inst_id.to_string(),
                s_ordertag_requests.size());

        SERVER_ILOG("AlgoServer_SendOrderTagNewOrderSingle: Sent OTD NOS: "
                "algo_inst_id=%s",
                algo_inst_id.to_string());

        SERVER_WLOG("AlgoServer_SendOrderTagNewOrderSingle: Error sending OTD NOS: LBM Request Ptr is null but OTD error is not fatal: "
                    "algo_inst_id=%s",
                    algo_inst_id.to_string());

        SERVER_WLOG("SendExportValue: Encode failed: %s account_id=%llu", ALGOSERVER_ERROR_STR[algoserver_error], connection_id);

        SERVER_ILOG("using the following string %s to generate session id: 0x%016llX",
        std::string(topic_name),
        session_id);