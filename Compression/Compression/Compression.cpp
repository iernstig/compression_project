// Most stupid LZ 77-type compressor out there.


#include "pch.h"
#include "stdio.h"
#include "stdint.h"
#include "stdlib.h"
#include "string.h"
#include <vector>
#define assert(x) do{if(!(x))__debugbreak();}while(0)

struct BitReader
{
	uint64_t buffer;
	int buffer_bits;
	uint64_t *stream;

	uint64_t read_bits(int num_bits) {
		uint64_t data = 0;
		for (int i = 0; i < num_bits; i++)
		{
			if (buffer_bits == 0)
			{
				buffer_bits = 64;
				buffer = *(stream++);
			}
			data <<= 1;
			data |= buffer & 1;
			buffer >>= 1;
			--buffer_bits;
		}
		return data;
	}
};

struct BitWriter
{
	uint64_t buffer;
	int buffer_bits;
	std::vector<uint64_t> stream;
	void push_bits(uint64_t data, int num_bits) {
		for (int i = num_bits - 1; i >= 0; i--)
		{
			buffer |= ((data >> i) & 1) << (buffer_bits);
			++buffer_bits;
			if (buffer_bits == 64)
			{
				stream.push_back(buffer);
				buffer = 0;
				buffer_bits = 0;
			}
		}
	}

	void *to_bytes(int *num_bytes)
	{
		stream.push_back(buffer);
		*num_bytes = sizeof(uint64_t)*stream.size();
		char *data = (char*)malloc(*num_bytes);
		memcpy(data, &stream[0], *num_bytes);
		return data;
	}
};

BitWriter make_bitwriter() {
	BitWriter ret = {};
	return ret;
}

BitReader make_bitreader(void *data) {
	BitReader ret = {};
	ret.stream = (uint64_t *)data;
	return ret;
}


const int max_match_length = 1 << 16;
const int max_match_dist = 1 << 15;
struct MatchResult {
	bool is_literal;
	union {
		struct
		{
			uint16_t start;
			uint16_t length;
		};
		char literal;
	};
};


struct HuffNode
{
	int left, right;
	int value, freq;
};
#include <algorithm>
#include <queue>


struct Code
{
	int bits, length;
};

struct HuffTree
{
	std::vector<HuffNode> nodes;
	int num_leaves;
	int root;
	Code *code_table;

	int decode_next_symbol(BitReader *br)
	{
		int node_idx = root;
		while (node_idx >= num_leaves){
			bool left = br->read_bits(1);
			node_idx = left ? nodes[node_idx].left : nodes[node_idx].right;
		}
		return nodes[node_idx].value;
	}

	void encode_symbol(BitWriter *bw, int symbol)
	{
		Code c = code_table[symbol];
		bw->push_bits(c.bits, c.length);
	}


};


void compute_codes(HuffTree tree)
{
	std::vector<std::pair<int,Code>> to_process;
	to_process.push_back({ tree.root,{} });
	tree.code_table[tree.root] = {};
	while(to_process.size() > 0)
	{
		int node_idx = to_process.back().first;
		Code c = to_process.back().second;
		to_process.pop_back();
		HuffNode n = tree.nodes[node_idx];
		if (n.left != -1)
		{
			Code cc = { c.bits<<1  | 1, c.length + 1 };
			if (n.left < tree.num_leaves)
				tree.code_table[tree.nodes[n.left].value] = cc;
			else
				to_process.push_back({ n.left, cc });
		}

		if (n.right != -1)
		{
			Code cc = { c.bits<<1, c.length + 1 };
			if (n.right < tree.num_leaves)
				tree.code_table[tree.nodes[n.right].value] =cc;
			else
				to_process.push_back({n.right, cc});
		}
	}

}

#include "intrin.h"

HuffTree create_huff_tree(int *freqs, int num_elems)
{
	std::vector<HuffNode> nodes;
	int num_leaves = 0;
	for (int i = 0; i < num_elems; i++){
		if (freqs[i] != 0) {
			HuffNode node = {};
			node.value = i;
			node.freq = freqs[i];
			node.left = node.right = -1;
			nodes.push_back(node);
			++num_leaves;
		}
	}
	//sort based on freqency.
	auto comp = [freqs](const auto& a, const auto& b) {return a.freq < b.freq;};
	std::sort(nodes.begin(), nodes.end(), comp);

	std::queue<int> a, b;
	for (int i = 0; i < nodes.size();i++) a.push(i);

	while(a.size() + b.size() > 1)
	{
		int chosen_nodes[2];
		for (int j = 0; j < 2; j++)
		{
			if (a.size() == 0) {
				chosen_nodes[j] = b.front();
				b.pop();
			} else if (b.size() == 0) {
				chosen_nodes[j] = a.front();
				a.pop();
			}
			else
			{
				if (nodes[a.front()].freq <= nodes[b.front()].freq){
					chosen_nodes[j] = a.front();
					a.pop();
				}
				else {
					chosen_nodes[j] = b.front();
					b.pop();
				}
			}
		}

		HuffNode new_node = {};
		new_node.left = chosen_nodes[0];
		new_node.right = chosen_nodes[1];
		new_node.freq = nodes[new_node.left].freq + nodes[new_node.right].freq;

		b.push(nodes.size());
		nodes.push_back(new_node);
	}

	HuffTree ret = {};
	ret.num_leaves = num_leaves;
	ret.nodes = nodes;
	ret.root = nodes.size() == 1 ? a.front() : b.front();
	ret.code_table = (Code *)malloc(sizeof(Code)*num_elems);
	memset(ret.code_table, 0xff, sizeof(Code)*num_elems);


	compute_codes(ret);
	return ret;
}








HuffTree ht = {};
HuffTree ht_2 = {};

// pack the match results with huffman-trees
void* compress_match_result(std::vector<MatchResult> results, int *num_bytes){
	auto bw = make_bitwriter();
	bw.push_bits(results.size(), 32);


	static int freqs[256+32];
	static int freqs_2[32];


	for (MatchResult m : results)
	{
		if (m.is_literal) {
			freqs[(uint8_t)m.literal]++;
		}
		else {
			unsigned long fst_idx;
			_BitScanReverse(&fst_idx, m.length);
			freqs[256 + fst_idx]++;

			
			_BitScanReverse(&fst_idx, m.length);
			freqs[256 + fst_idx]++;

			//use separate huff tree here (we're implied if we get a match length)
			_BitScanReverse(&fst_idx, m.start+1);
			freqs_2[fst_idx]++;
		}
	}
	printf("freqs = ");
	for (int i = 0; i < 256+32; i++){
		printf("%d ", freqs[i]);
	}
	printf("\n");


	printf("creating the huff tree!\n");
	ht = create_huff_tree(freqs, 256+32);
	ht_2 = create_huff_tree(freqs_2, 32);


#if 1
	//Huffman info:
	int pre_bits = 0;
	int post_bits = 0;
	int sum = 0;
	for (int i = 0; i < 256+32; i++)
	{
		post_bits += freqs[i] * ht.code_table[i].length;
		pre_bits += freqs[i] * 8;
		sum += freqs[i];
	}
	printf("pre_bits: %d, post_bits %d\n", pre_bits, post_bits);
	float optimal = 0;
	for (int i = 0; i < 256+32; i++)
	{
		if (freqs[i] != 0)
		{
			optimal += freqs[i] * log2(sum / freqs[i]);
		}

	}
	printf("entropy = %f bits\n", optimal);

	printf("saving %d bytes - size of huff tree\n", (pre_bits-post_bits)/8);

#endif

	for (MatchResult m : results){
		if (m.is_literal){
			ht.encode_symbol(&bw, (uint8_t) m.literal);
		}
		else {

			unsigned long fst_idx;
			_BitScanReverse(&fst_idx, m.length);
			ht.encode_symbol(&bw, fst_idx+256);
			bw.push_bits(m.length, fst_idx);



			_BitScanReverse(&fst_idx, m.start+1);
			ht_2.encode_symbol(&bw, fst_idx);
			bw.push_bits(m.start+1, fst_idx);
		}
	}

	return bw.to_bytes(num_bytes);
}



void decompress_match_results(void *data, std::vector<MatchResult> *results) {
	
	auto br = make_bitreader(data);
	int num_matches = br.read_bits(32);

	for (int i = 0; i < num_matches; i++){
		MatchResult m = {};
		int symbol = ht.decode_next_symbol(&br);
		m.is_literal = symbol < 256;

		if (m.is_literal) {
			m.literal = symbol;
		}
		else {
			symbol -= 256;
			int fst_set = symbol;
			m.length = (1 << fst_set) | br.read_bits(fst_set);
			
			
			fst_set = ht_2.decode_next_symbol(&br);
			m.start = ((1 << fst_set) | br.read_bits(fst_set))-1;

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
	if (ret.length < 4) // this constant is an approximation of when a copy is worth more just inserting a literal into the steam.
	{
		ret = {};
		ret.is_literal = true;
		ret.literal = **data;
		*data += 1;
	}
	else {
		*data += ret.length;
	}
	return ret;
}


void add_match(CircularBuffer *buffer, MatchResult match, std::vector<char> *vec)
{
	if (match.is_literal) {
		char byte = match.literal;
		buffer->add_char(byte);
		if (vec) vec->push_back(byte);
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


void *encode(char *data, int len, int *num_bytes)
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
#if 1
	// enable if encoding is fucked. helps debugging.
	std::vector<MatchResult> match_results_2;
	decompress_match_results(ret, &match_results_2);
	for (int i = 0; i < match_results.size(); i++)
	{
		assert(match_results[i].is_literal == match_results_2[i].is_literal);
		assert(match_results[i].length == match_results_2[i].length);
		assert(match_results[i].start == match_results_2[i].start);
	}
#endif

	return ret;
}

void decode(void *encoded_data, int num_bytes, std::vector<char> *decoded_data)
{
	CircularBuffer buff = alloc_buffer(max_match_dist);
	std::vector<MatchResult> match_results;
	decompress_match_results(encoded_data, &match_results);
	for (auto m : match_results){
		add_match(&buff, m, decoded_data);
	}
}

char *read_file(const char *path)
{
	FILE *f;
	fopen_s(&f, path, "rb");
	fseek(f, 0, SEEK_END);
	long fsize = ftell(f);
	fseek(f, 0, SEEK_SET); 

	char *string = (char *)malloc(fsize + 1);
	fread(string, fsize, 1, f);
	fclose(f);
	return string;
}



int main()
{
#if 0
	{
		int freqs[] = {8, 4, 2, 1, 1};
		auto tree = create_huff_tree(freqs, 5);
		
		
		BitWriter bw = make_bitwriter();
		tree.encode_symbol(&bw, 1);
		tree.encode_symbol(&bw, 0);

		int num_bytes;
		BitReader br = make_bitreader(bw.to_bytes(&num_bytes));

		int a = tree.decode_next_symbol(&br);
		int b = tree.decode_next_symbol(&br);

		return 0;
	}
#endif


#if 0
	char *data = (char*) "Lorem Ipsum is simply dummy text of the printing and typesetting industry. Lorem Ipsum has been the industry's standard dummy text ever since the 1500s, when an unknown printer took a galley of type and scrambled it to make a type specimen book. It has survived not only five centuries, but also the leap into electronic typesetting, remaining essentially unchanged. It was popularised in the 1960s with the release of Letraset sheets containing Lorem Ipsum passages, and more recently with desktop publishing software like Aldus PageMaker including versions of Lorem Ipsum"
		"It is a long established fact that a reader will be distracted by the readable content of a page when looking at its layout. The point of using Lorem Ipsum is that it has a more-or-less normal distribution of letters, as opposed to using 'Content here, content here', making it look like readable English. Many desktop publishing packages and web page editors now use Lorem Ipsum as their default model text, and a search for 'lorem ipsum' will uncover many web sites still in their infancy. Various versions have evolved over the years, sometimes by accident, sometimes on purpose (injected humour and the like)."
		"Contrary to popular belief, Lorem Ipsum is not simply random text. It has roots in a piece of classical Latin literature from 45 BC, making it over 2000 years old. Richard McClintock, a Latin professor at Hampden-Sydney College in Virginia, looked up one of the more obscure Latin words, consectetur, from a Lorem Ipsum passage, and going through the cites of the word in classical literature, discovered the undoubtable source. Lorem Ipsum comes from sections 1.10.32 and 1.10.33 of \"de Finibus Bonorum et Malorum\" (The Extremes of Good and Evil) by Cicero, written in 45 BC. This book is a treatise on the theory of ethics, very popular during the Renaissance. The first line of Lorem Ipsum, \"Lorem ipsum dolor sit amet..\", comes from a line in section 1.10.32.";
#elif 0 
	char *data = (char *) "hello hello hello hello";
#else 
	const char *path = "C:\\Users\\Danie\\compression\\compression_project\\pride_and_predujudice.txt";
	char *data = read_file(path);
#endif

	std::vector<char> decoded_data;
	int num_data_bytes = strlen(data) + 1;
	int num_bytes;
	void *encoded_data = encode(data, num_data_bytes, &num_bytes);
	decode(encoded_data, num_bytes, &decoded_data);

	

	printf("Compressed %d bytes of data to %d bytes\n", num_data_bytes, num_bytes);
	printf("_Insane_ Compression Ratio of: %f\n", num_bytes/(float)num_data_bytes);


	for (int i = 0; i < num_data_bytes; i++) {
		assert(data[i] == decoded_data[i]);
	}
	getchar();
}

