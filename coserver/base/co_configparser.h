#ifndef _CO_CONFIGPARSER_H_
#define _CO_CONFIGPARSER_H_

#include "base/co_config.h"


namespace coserver
{
/*
 * line args meaning single line end with special character ';', '{' or '}'
 * 'args' without special character above
 */
struct CoLineArgs
{
    std::vector<std::string> m_args;
    int32_t m_lineno;
};

/* 
 * block args meaning 
 * 1.single-line end with character ';'
 * 2.muti-line with first line end with character '{', 
 *   and last line end with characher '}'
 *
 * first word of block is 'name', first line number is 'm_lineno'
 */
struct CoBlockArgs
{
    std::string m_blockName; // block name
    int32_t m_lineno;
    std::vector<CoLineArgs> m_multiLineArgs;
};


class CoConfigParser 
{
public:
    CoConfigParser();
    ~CoConfigParser();
    
    const CoConfig* get_config() const {
        return &m_config;
    }

    int32_t parse_configfile(const std::string &configfile);


private:
    int32_t parse_content(const std::string &content);
    int32_t read_allblock(const std::string &content, std::vector<CoBlockArgs> &multiBlocks);

    bool parse_config_conf(const CoBlockArgs &blockArgs);
    bool parse_config_hook(const CoBlockArgs &blockArgs);
    bool parse_config_server(const CoBlockArgs &blockArgs);
    bool parse_config_upstream(const CoBlockArgs &blockArgs);

private:
    CoConfig    m_config;
};

}

#endif //_CO_CONFIGPARSER_H_

