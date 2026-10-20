#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ostream>

namespace Rbs::CoupledDiagnosis
{
    // Independent standard LZ4 blocks around the unchanged logical record stream.
    // No dictionary crosses a block; all storage belongs to the recorder Session.
    struct LosslessStream
    {
        static constexpr std::size_t BlockSize=64*1024, OutputSize=66*1024, HashSize=16*1024;
        static constexpr std::uint64_t FnvBasis=14695981039346656037ull, FnvPrime=1099511628211ull;
        enum class Error { None, Capacity, Io, Record };
        std::array<unsigned char,BlockSize> input{};
        std::array<unsigned char,OutputSize> output{};
        std::array<std::uint32_t,HashSize> table{};
        std::size_t used{},pendingRecords{};
        std::uint64_t logicalBytes{},logicalRecords{},logicalFnv{FnvBasis},pendingFnv{FnvBasis};
        std::uint64_t physicalBytes{8},blocks{},compressionNs{};
        Error error{Error::None};

        static std::uint32_t Word(const unsigned char* p) noexcept {
            return std::uint32_t(p[0])|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24);
        }
        static std::size_t Hash(const unsigned char* p) noexcept {return ((Word(p)*2654435761u)>>19)*2;}
        static void U32(unsigned char* p,std::uint32_t v) noexcept {
            for(unsigned i=0;i<4;++i)p[i]=static_cast<unsigned char>(v>>(8*i));
        }
        void Length(std::size_t value,std::size_t& out) noexcept {
            while(value>=255){output[out++]=255;value-=255;}output[out++]=static_cast<unsigned char>(value);
        }
        std::size_t Compress() noexcept {
            table.fill(0xffffffffu);std::size_t cursor=0,anchor=0,out=0;
            // LZ4 block constraints: final five bytes are literals and the final
            // match starts at least twelve bytes before the end of the block.
            while(cursor+12<=used){
                const auto h=Hash(input.data()+cursor),recent=std::size_t(table[h]),previous=std::size_t(table[h+1]);
                table[h+1]=table[h];table[h]=static_cast<std::uint32_t>(cursor);
                std::size_t candidate=0xffffffffu,length=0;
                for(unsigned rank=0;rank<2;++rank){
                    const auto probe=rank==0?recent:previous;
                    if(probe>=cursor||cursor-probe>65535||Word(input.data()+probe)!=Word(input.data()+cursor))continue;
                    std::size_t size=4;
                    while(cursor+size<used-5&&input[probe+size]==input[cursor+size])++size;
                    if(size>length){candidate=probe;length=size;}
                }
                if(!length){++cursor;continue;}
                while(cursor>anchor&&candidate>0&&input[cursor-1]==input[candidate-1]){--cursor;--candidate;++length;}
                const auto literals=cursor-anchor,match=length-4;
                output[out++]=static_cast<unsigned char>(((literals<15?literals:15)<<4)|(match<15?match:15));
                if(literals>=15)Length(literals-15,out);
                std::memcpy(output.data()+out,input.data()+anchor,literals);out+=literals;
                const auto offset=cursor-candidate;output[out++]=static_cast<unsigned char>(offset);output[out++]=static_cast<unsigned char>(offset>>8);
                if(match>=15)Length(match-15,out);
                const auto end=cursor+length;
                for(std::size_t p=cursor+1;p<end&&p+12<=used;++p){
                    const auto slot=Hash(input.data()+p);
                    table[slot+1]=table[slot];table[slot]=static_cast<std::uint32_t>(p);
                }
                cursor=anchor=end;
            }
            const auto literals=used-anchor;output[out++]=static_cast<unsigned char>((literals<15?literals:15)<<4);
            if(literals>=15)Length(literals-15,out);
            std::memcpy(output.data()+out,input.data()+anchor,literals);return out+literals;
        }
        bool Flush(std::ostream& stream,std::size_t& combined,std::size_t limit) noexcept {
            if(error!=Error::None)return false;if(!used)return true;
            const auto start=std::chrono::steady_clock::now();const auto encoded=Compress();
            compressionNs+=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count());
            const bool compressed=encoded<used;const auto stored=compressed?encoded:used;
            if(combined>limit||8+stored>limit-combined){error=Error::Capacity;return false;}
            unsigned char header[8];U32(header,static_cast<std::uint32_t>(used));U32(header+4,static_cast<std::uint32_t>(stored));
            stream.write(reinterpret_cast<const char*>(header),8);
            stream.write(reinterpret_cast<const char*>(compressed?output.data():input.data()),static_cast<std::streamsize>(stored));
            if(!stream){error=Error::Io;return false;}
            combined+=8+stored;physicalBytes+=8+stored;++blocks;
            logicalBytes+=used;logicalRecords+=pendingRecords;logicalFnv=pendingFnv;used=pendingRecords=0;return true;
        }
        bool WriteRecord(std::uint8_t kind,std::uint16_t raw,const unsigned char* packed,std::size_t size,
            std::ostream& stream,std::size_t& combined,std::size_t limit) noexcept {
            if(error!=Error::None)return false;
            if(size>65535||5+size>BlockSize){error=Error::Record;return false;}
            if(used+5+size>BlockSize&&!Flush(stream,combined,limit))return false;
            const auto begin=used;input[used++]=kind;input[used++]=static_cast<unsigned char>(raw);input[used++]=static_cast<unsigned char>(raw>>8);
            input[used++]=static_cast<unsigned char>(size);input[used++]=static_cast<unsigned char>(size>>8);
            std::memcpy(input.data()+used,packed,size);used+=size;++pendingRecords;
            for(auto i=begin;i<used;++i){pendingFnv^=input[i];pendingFnv*=FnvPrime;}return true;
        }
        void Metadata(std::ostream& out) const {
            // Counts and hash describe complete emitted blocks, including on
            // failure. An unflushed tail is disclosed separately, never PASS.
            out<<"{\"logical_bytes\":"<<logicalBytes<<",\"logical_records\":"<<logicalRecords
                <<",\"logical_fnv1a64\":"<<logicalFnv<<",\"physical_bytes\":"<<physicalBytes
                <<",\"blocks\":"<<blocks<<",\"buffered_logical_bytes\":"<<used
                <<",\"compression_ns\":"<<compressionNs<<'}';
        }
    };
    static_assert(LosslessStream::OutputSize>=LosslessStream::BlockSize+LosslessStream::BlockSize/255+16);
    static_assert(sizeof(LosslessStream)<200*1024,"Fixed compressor storage exceeds declared 200 KiB per stream");
}
