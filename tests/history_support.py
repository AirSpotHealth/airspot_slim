from native_support import ROOT
import re

def declarations():
    data=(ROOT/'application/include/services/data_log.h').read_text()
    enum=re.search(r'typedef enum \{.*?\} record_type_t;', data, re.S).group(0)
    return '#include <stdint.h>\n#include <stddef.h>\n' + enum + '\nstruct history_event {uint16_t value; record_type_t type;};\n'

STUBS = declarations() + r'''
static unsigned history_count;
static struct history_event history_records[256];
static int history_event_record(uint16_t v, record_type_t t) {
    assert(history_count<256);
    history_records[history_count++]=(struct history_event){v,t};return 0;
}
static int history_event_batch(const struct history_event *e,size_t n) {
    for(size_t i=0;i<n;i++)history_event_record(e[i].value,e[i].type);return 0;
}
'''
