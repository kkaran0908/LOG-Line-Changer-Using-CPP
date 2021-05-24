        TTLOG(ERROR,13)<<"Cannot change the LbmSenderMode because use_lbm_transaction_logger="<<s_use_lbm_transaction_logger<<endl;

        TTLOG(INFO,13)<<"SetLbmSenderMode to "<<useUMP?"UMP":"LBM"<<endl;

        TTLOG(ERROR,13)<<"Error creating child sender. market_id="<<mkt_id<<" account_id="<<account_id<<"lu error="<<ex.what()<<endl;

    TTLOG(INFO,13)<<"AlgoServer_EmplaceOrderTagRequestPtr: Emplaced ptr: algo_inst_id="<<algo_inst_id<<" s_ordertag_requests.size()="<<s_ordertag_requests.size()<<endl;

        TTLOG(INFO,13)<<"AlgoServer_SendOrderTagNewOrderSingle: Sent OTD NOS: algo_inst_id="<<algo_inst_id<<endl;

        TTLOG(WARNING,13)<<"AlgoServer_SendOrderTagNewOrderSingle: Error sending OTD NOS: LBM Request Ptr is null but OTD error is not fatal: algo_inst_id="<<algo_inst_id<<endl;

        TTLOG(WARNING,13)<<"SendExportValue: Encode failed: "<<ALGOSERVER_ERROR_STR[algoserver_error]<<" account_id="<<connection_id<<"lu"<<endl;

    TTLOG(INFO,13)<<"using the following string "<<topic_name<<" to generate session id: 0x"<<std::showbase << std::hex<<session_id<<"16llX"<<endl;

        TTLOG(INFO,13)<<"AlgoServer_EmplaceOrderTagRequestPtr: Emplaced ptr: algo_inst_id="<<algo_inst_id<<" s_ordertag_requests.size()="<<s_ordertag_requests.size()<<endl;

        TTLOG(INFO,13)<<"AlgoServer_SendOrderTagNewOrderSingle: Sent OTD NOS: algo_inst_id="<<algo_inst_id<<endl;

        TTLOG(WARNING,13)<<"AlgoServer_SendOrderTagNewOrderSingle: Error sending OTD NOS: LBM Request Ptr is null but OTD error is not fatal: algo_inst_id="<<algo_inst_id<<endl;

        TTLOG(WARNING,13)<<"SendExportValue: Encode failed: "<<ALGOSERVER_ERROR_STR[algoserver_error]<<" account_id="<<connection_id<<"lu"<<endl;

        TTLOG(INFO,13)<<"using the following string "<<topic_name<<" to generate session id: 0x"<<std::showbase << std::hex<<session_id<<"16llX"<<endl;
