// Most stupid LZ-type compressor out there.


#include "pch.h"
#include "stdio.h"
#include "stdint.h"
#include "stdlib.h"
#include "string.h"
#include <vector>
#define assert(x) do{if(!(x))__debugbreak();}while(0)

// todo remove the literal buffer and do the lzw things.
// is there a fast way to enforce that we don't overwrite previous words?

struct MatchResult {
	bool is_literal;
	union {
		struct
		{
			uint16_t start;
			uint16_t length;
		};
		struct {
			char literal_buffer[4];
		};
	};

};


int min(int a, int b)
{
	return a < b ? a : b;
}

struct CircularBuffer {
	int capacity;
	char *data;
	int length;
	int curr;


	void add_char(char c)
	{
		length = min(length + 1, capacity);
		data[curr] = c;
		curr = (curr+1) & (capacity - 1);
	}

	char get_byte(int v)
	{
		int i = (curr-v-1) & (capacity - 1);
		return data[i];
	}
};

CircularBuffer alloc_buffer(int cap)
{
	// cap better be a power of two.
	CircularBuffer buff = {};
	buff.capacity = cap;
	buff.data = (char *)malloc(cap);
	return buff;
}


MatchResult find_next_match(CircularBuffer buff, char **data, int data_len)
{
	// this is stupid but whatever.
	// also we should allow the length to be longer than the buffer for repetitions.
	MatchResult ret = { };
	int i, j;
	i = j = 0;
	for (i = 0; i < buff.length; i++){
		for (j = 0; j <= min(i,data_len); j++)
		{
			char data_byte = (*data)[j];
			char buff_byte = buff.get_byte(i - j);

			if ((*data)[j] != buff.get_byte(i - j)){
				if (ret.length < j){
					ret.start = i;
					ret.length = j;
				}
				break;
			}
		}
	}

	if (ret.length < j) {
		ret.start = i;
		ret.length = j;
	}
	if (ret.length == 0)
	{
		ret.is_literal = true;
		int cpy_len = min(data_len, sizeof(ret.literal_buffer));
		memcpy(ret.literal_buffer, *data, cpy_len);
		*data += cpy_len;
	}
	else {
		*data += ret.length;
	}
	return ret;
}


void add_match(CircularBuffer *buffer, MatchResult match, std::vector<char> *vec)
{
	if (match.is_literal) {
		for (int i = 0; i < sizeof(match.literal_buffer); i++){
			char byte = match.literal_buffer[i];
			buffer->add_char(byte);
			if (vec) vec->push_back(byte);
		}
	}
	else
	{
		for (int i = 0; i < match.length; i++)
		{
			char byte = buffer->get_byte(match.start - 1);
			buffer->add_char(byte);
			if (vec) vec->push_back(byte);
		}
	}
}


void encode(char *data, int len, std::vector<MatchResult> *encoded_data)
{
	// We'll zero pad if we end on a literal. 
	// we should add a few bits in the header to adress this. (or do the lzw thing)

	char *data_end = data + len;
	CircularBuffer buff = alloc_buffer(64);
	while(data != data_end)
	{
		auto match = find_next_match(buff, &data, data_end- data);
		add_match(&buff, match, NULL);
		encoded_data->push_back(match);
	}
}

void decode(std::vector<MatchResult> *encoded_data, std::vector<char> *decoded_data)
{
	CircularBuffer buff = alloc_buffer(64);
	for (auto m : *encoded_data)
	{
		add_match(&buff, m, decoded_data);
	}
}



int main()
{
	CircularBuffer buff = alloc_buffer(64);

	char *data = (char*) " hellloo hellloo hellloo hellloo hellloo hellloo hellloo hellloo";
	
	std::vector<MatchResult> encoded_data;
	std::vector<char> decoded_data;
	encode(data, strlen(data), &encoded_data);
	decode(&encoded_data, &decoded_data);
	for (int i = 0; i < decoded_data.size(); i++) {
		assert(data[i] == decoded_data[i]);
	}

	printf("compressed %d bytes of data to %d bytes\n", strlen(data), encoded_data.size() * sizeof(MatchResult));
	getchar();
}

