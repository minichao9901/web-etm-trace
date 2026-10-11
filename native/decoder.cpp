// OpenCSD integration. Output is JSONL; no inferred instructions are invented here.
#include "opencsd/c_api/opencsd_c_api.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
#include <stdexcept>
#include <iomanip>

static std::string quote(const char *text) {
    std::ostringstream s; s << '"';
    for (const unsigned char *p=(const unsigned char *)text; *p; ++p) {
        if (*p=='"' || *p=='\\') s << '\\' << *p;
        else if (*p=='\n') s << "\\n";
        else if (*p=='\r') s << "\\r";
        else if (*p=='\t') s << "\\t";
        else if (*p<32) s << "\\u" << std::hex << std::setw(4) << std::setfill('0') << (unsigned)*p << std::dec;
        else s << *p;
    }
    s << '"'; return s.str();
}
static ocsd_datapath_resp_t element(const void *, ocsd_trc_index_t index, uint8_t id, const ocsd_generic_trace_elem *e) {
    char text[2048] = {0}; ocsd_gen_elem_str(e,text,sizeof(text));
    std::cout << "{\"kind\":\"element\",\"offset\":" << index << ",\"id\":" << (unsigned)id
      << ",\"type\":" << (int)e->elem_type << ",\"start\":" << e->st_addr << ",\"end\":" << e->en_addr
      << ",\"isa\":" << (int)e->isa << ",\"flags\":" << e->flag_bits
      << ",\"cycleCount\":" << e->cycle_count << ",\"timestamp\":" << e->timestamp
      << ",\"lastExecuted\":" << e->last_instr_exec << ",\"lastConditional\":" << e->last_instr_cond
      << ",\"lastType\":" << (int)e->last_i_type << ",\"lastSubtype\":" << (int)e->last_i_subtype
      << ",\"payload\":" << e->num_instr_range << ",\"text\":" << quote(text) << "}\n";
    return OCSD_RESP_CONT;
}
static void packet(const void *, ocsd_datapath_op_t op, ocsd_trc_index_t index, const void *pkt, uint32_t size, const uint8_t *data) {
    if(op!=OCSD_OP_DATA || !pkt) return;
    char text[2048]={0}; ocsd_pkt_str(OCSD_PROTOCOL_ETMV4I,pkt,text,sizeof(text));
    std::ostringstream hex;
    for(uint32_t i=0;i<size;i++) hex << std::hex << std::setw(2) << std::setfill('0') << (unsigned)data[i];
    std::cout << "{\"kind\":\"packet\",\"offset\":" << index << ",\"size\":" << size
      << ",\"hex\":" << quote(hex.str().c_str()) << ",\"text\":" << quote(text) << "}\n";
}
static std::vector<uint8_t> read(const std::string &path) {
    std::ifstream f(path.c_str(),std::ios::binary);
    if(!f) throw std::runtime_error("Cannot read "+path);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());
}
static void check(ocsd_err_t code) { if(code!=OCSD_OK) throw std::runtime_error("OpenCSD API error "+std::to_string(code)); }
int main(int argc,char **argv) {
    if(argc!=3) { std::cerr << "Usage: etm_decode trace.bin config-and-memory.txt\n"; return 2; }
    dcd_tree_handle_t tree=0;
    try {
        std::ifstream config(argv[2]); if(!config) throw std::runtime_error("Cannot read config");
        uint32_t regs[11]; std::string value;
        for(unsigned i=0;i<11;i++) { if(!(config>>value)) throw std::runtime_error("Missing register"); regs[i]=std::stoul(value,0,0); }
        ocsd_etmv4_cfg c={};
        c.reg_idr0=regs[0]; c.reg_idr1=regs[1]; c.reg_idr2=regs[2];
        c.reg_idr8=regs[3]; c.reg_idr9=regs[4]; c.reg_idr10=regs[5]; c.reg_idr11=regs[6]; c.reg_idr12=regs[7]; c.reg_idr13=regs[8];
        c.reg_configr=regs[9]; c.reg_traceidr=regs[10]; c.arch_ver=ARCH_V7; c.core_prof=profile_CortexM;
        tree=ocsd_create_dcd_tree(OCSD_TRC_SRC_SINGLE,0); if(!tree) throw std::runtime_error("No decode tree");
        check(ocsd_dt_set_gen_elem_outfn(tree,element,0));
        unsigned char id=0;
        check(ocsd_dt_create_decoder(tree,OCSD_BUILTIN_DCD_ETMV4I,OCSD_CREATE_FLG_FULL_DECODER,&c,&id));
        check(ocsd_dt_attach_packet_callback(tree,id,OCSD_C_API_CB_PKT_MON,(void *)packet,0));
        std::vector<std::vector<uint8_t> > images;
        while(config>>value) {
            uint64_t address=std::stoull(value,0,0); std::string file; std::getline(config,file);
            file=file.substr(file.find_first_not_of(" \t")); if(!file.empty() && file.back()=='\r') file.pop_back();
            images.push_back(read(file));
            check(ocsd_dt_add_buffer_mem_acc(tree,address,OCSD_MEM_SPACE_ANY,images.back().data(),(uint32_t)images.back().size()));
        }
        const auto raw=read(argv[1]); uint32_t done=0;
        auto response=ocsd_dt_process_data(tree,OCSD_OP_DATA,0,(uint32_t)raw.size(),raw.data(),&done);
        if(response>=OCSD_RESP_FATAL_NOT_INIT || done!=raw.size()) throw std::runtime_error("Decode stopped at "+std::to_string(done)+" / "+std::to_string(raw.size())+" response "+std::to_string((int)response));
        response=ocsd_dt_process_data(tree,OCSD_OP_EOT,done,0,0,0);
        if(response>=OCSD_RESP_FATAL_NOT_INIT) throw std::runtime_error("Decoder end-of-trace error");
        char error[4096]={0}; uint8_t chan=0; ocsd_trc_index_t index=0;
        auto code=ocsd_get_last_err(&index,&chan,error,sizeof(error));
        std::cout << "{\"kind\":\"status\",\"processed\":" << done << ",\"library\":" << quote(ocsd_get_version_str())
          << ",\"lastErrorCode\":" << (int)code << ",\"lastError\":" << quote(error) << "}\n";
        ocsd_destroy_dcd_tree(tree); return 0;
    } catch(const std::exception &e) { if(tree) ocsd_destroy_dcd_tree(tree); std::cerr << e.what() << '\n'; return 1; }
}
