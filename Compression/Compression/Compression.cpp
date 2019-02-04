// Most stupid LZ77-type compressor out there.


#include "pch.h"
#include "stdio.h"
#include "stdint.h"
#include "stdlib.h"
#include "string.h"
#include <vector>
#define assert(x) do{if(!(x))__debugbreak();}while(0)

const int max_match_length = 1 << 8;
const int max_match_dist = 1 << 15;
struct MatchResult {
	bool is_literal;
	union {
		struct
		{
			uint16_t start;
			uint16_t length;
		};
		struct {
			char literal_buffer[2];
		};
	};
};

// Just pack the match results tightly.
// I suppose we chould shove some huffman in here?

// for literals we currently waste a 7 bits for each Match.
// ie literals work on byte level (and we need one additional bit to indicate weather it's a bit or not.
// Just do a bitstream I suppose. wastes around 150 bytes for the current stuff. ie we'd go form 1% compresson to ~8.5% 
char *compress_match_result(std::vector<MatchResult> results, int *num_bytes){
	{
		MatchResult mm;
		// otherwise the below allocation is invalid.
		assert(sizeof(mm.literal_buffer) == 2);
	}

	*num_bytes = 3 * results.size();
	char *ret = (char *) malloc(*num_bytes);
	char *w = ret;
	int i = 0;
	for (MatchResult m : results){
		if (m.is_literal){
			*(w++) = (m.is_literal) << 7;
			for (int i = 0; i < sizeof(m.literal_buffer); i++){
				*(w++) = m.literal_buffer[i];
			}
		}
		else {
			uint32_t tmp = 0;
			tmp |= m.start << 8;
			tmp |= m.length;
			*(uint32_t *)w = tmp;
			w += 3;
		}
	}
	return ret;
}

void decompress_match_results(char *data, int num_bytes, std::vector<MatchResult> *results) {
	for (int i = 0; i < num_bytes / 3; i++){
		uint32_t tmp = *(uint32_t *)&data[i * 3];
		uint8_t * d = (uint8_t *)&data[i * 3];
		MatchResult m = {};
		m.is_literal = d[0] == 0x80;

		if (d[0] == 0x80) {
			for (int i = 0; i < sizeof(m.literal_buffer); i++) {
				m.literal_buffer[i] = d[i+1];
			}
		}
		else {
			m.start = (tmp >> 8) & ((1<<15)-1);
			m.length = tmp & ((1 << 8) - 1);
		}
		results->push_back(m);
	}
}



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
	MatchResult ret = { };
	int i, j;
	i = j = 0;
	for (i = 1; i < min(buff.length, max_match_dist); i++){
		for (j = 0; j <= min(data_len, max_match_length); j++)
		{
			char data_byte = (*data)[j];
			char buff_byte = buff.get_byte(i - j%(i+1));

			if ((*data)[j] != buff.get_byte(i - j%(i+1))){
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
	if (ret.length < sizeof(ret.literal_buffer))
	{
		ret = {};
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
			char byte = buffer->get_byte(match.start);
			buffer->add_char(byte);
			if (vec) vec->push_back(byte);
		}
	}
}


char *encode(char *data, int len, int *num_bytes)
{
	// We'll zero pad if we end on a literal. 
	// we should add a few bits in the header to address this. 
	std::vector<MatchResult> match_results;

	char *data_end = data + len;
	CircularBuffer buff = alloc_buffer(max_match_dist);
	while(data != data_end)
	{
		auto match = find_next_match(buff, &data, data_end- data);
		add_match(&buff, match, NULL);
		match_results.push_back(match);
	}
	
	auto ret = compress_match_result(match_results, num_bytes);
#if 0
	// enable if encoding is fucked. helps debugging.
	std::vector<MatchResult> match_results_2;
	decompress_match_results(ret, *num_bytes, &match_results_2);
	for (int i = 0; i < match_results.size(); i++)
	{
		assert(match_results[i].is_literal == match_results_2[i].is_literal);
		assert(match_results[i].length == match_results_2[i].length);
		assert(match_results[i].start == match_results_2[i].start);
	}
#endif

	return ret;
}

void decode(char *encoded_data, int num_bytes, std::vector<char> *decoded_data)
{
	CircularBuffer buff = alloc_buffer(max_match_dist);
	std::vector<MatchResult> match_results;
	decompress_match_results(encoded_data, num_bytes, &match_results);
	for (auto m : match_results){
		add_match(&buff, m, decoded_data);
	}
}



int main()
{
	CircularBuffer buff = alloc_buffer(64);

	char *data = (char*) "Lorem Ipsum is simply dummy text of the printing and typesetting industry. Lorem Ipsum has been the industry's standard dummy text ever since the 1500s, when an unknown printer took a galley of type and scrambled it to make a type specimen book. It has survived not only five centuries, but also the leap into electronic typesetting, remaining essentially unchanged. It was popularised in the 1960s with the release of Letraset sheets containing Lorem Ipsum passages, and more recently with desktop publishing software like Aldus PageMaker including versions of Lorem Ipsum"
		"It is a long established fact that a reader will be distracted by the readable content of a page when looking at its layout. The point of using Lorem Ipsum is that it has a more-or-less normal distribution of letters, as opposed to using 'Content here, content here', making it look like readable English. Many desktop publishing packages and web page editors now use Lorem Ipsum as their default model text, and a search for 'lorem ipsum' will uncover many web sites still in their infancy. Various versions have evolved over the years, sometimes by accident, sometimes on purpose (injected humour and the like)."
		"Contrary to popular belief, Lorem Ipsum is not simply random text. It has roots in a piece of classical Latin literature from 45 BC, making it over 2000 years old. Richard McClintock, a Latin professor at Hampden-Sydney College in Virginia, looked up one of the more obscure Latin words, consectetur, from a Lorem Ipsum passage, and going through the cites of the word in classical literature, discovered the undoubtable source. Lorem Ipsum comes from sections 1.10.32 and 1.10.33 of \"de Finibus Bonorum et Malorum\" (The Extremes of Good and Evil) by Cicero, written in 45 BC. This book is a treatise on the theory of ethics, very popular during the Renaissance. The first line of Lorem Ipsum, \"Lorem ipsum dolor sit amet..\", comes from a line in section 1.10.32.";

	std::vector<char> decoded_data;
	int num_data_bytes = strlen(data) + 1;
	int num_bytes;
	char *encoded_data = encode(data, num_data_bytes, &num_bytes);
	decode(encoded_data, num_bytes, &decoded_data);

	for (int i = 0; i < decoded_data.size(); i++) {
		assert(data[i] == decoded_data[i]);
	}

	printf("Compressed %d bytes of data to %d bytes\n", num_data_bytes, num_bytes);
	printf("_Insane_ Compression Ratio of: %f\n", num_bytes/(float)num_data_bytes);

	getchar();
}

