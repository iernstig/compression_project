// Most stupid LZ 77-type compressor out there.
#include "pch.h"


int min(int a, int b)
{
	return a < b ? a : b;
}

int max(int a, int b)
{
	return a > b ? a : b;
}
#define assert(x) do{if(!(x))__debugbreak();}while(0)

#include "stdio.h"
#include "stdint.h"
#include "stdlib.h"
#include "string.h"
#include <vector>
#include <algorithm>
#include <queue>
#include "intrin.h"

#include "SuffixArray.h"
#include "BitStream.h"
#include "HuffTrees.h"


// this is a hard threashold for the minimum match len.
// while the parser probably won't choose a too small match either way
// setting a hard min len allows more symbols for latter parts of the code. (ie min_match_len is always encoded in a separate symbol)

const int min_match_len = 5;


enum ActionType
{
	Action_Match,
	Action_Litrep,
	Action_Rep0, // grab the match distance from the last match.
};

struct Action {
	ActionType type = Action_Match;
	int distance;
	int length;
};

int num_bits_from_symbol(int symbol)
{
	assert(symbol < 32);
	assert(symbol >= 0);

	return max(symbol - 1, 0);
}

int symbol_from_int(int i, int min_value, int *bit_part)
{
	unsigned long fst_idx;
	i -= min_value;
	int is_set = _BitScanReverse(&fst_idx, i);

	int symbol = 0;
	if (is_set != 0) symbol = fst_idx + 1;

	if (bit_part)
		*bit_part = i;
	return symbol;
}

int int_from_symbol(int symbol, int min_value, int bit_part)
{
	int ret = 0;
	if (symbol != 0)
		ret = (1 << (symbol-1)) | bit_part;
	return ret + min_value;
}


#pragma optimize ("",off)
void create_huff_trees(char *data, HuffTree *literals, HuffTree *distances, HuffTree *action_lengths, HuffTree *lengths_post_litrep, std::vector<Action> &actions)
{
	static int freqs_literals[256];
	static int freqs_distances[32];
	static int freqs_action_lengths[64];
	static int freqs_lengths_post_litrep[64];


	memset(freqs_literals, 0, sizeof(freqs_literals));
	memset(freqs_distances, 0, sizeof(freqs_distances));
	memset(freqs_action_lengths, 0, sizeof(freqs_action_lengths));
	memset(freqs_lengths_post_litrep, 0, sizeof(freqs_lengths_post_litrep));

	bool last_litrep = false;
	int cursor = 0;
	for (Action a : actions)
	{
		if (a.type == Action_Litrep) {
			assert(!last_litrep);
			for (int i = 0; i < a.length; i++)
				freqs_literals[(uint8_t)data[cursor + i]]++;
			
			int len_symbol = symbol_from_int(a.length, 1, NULL);
			freqs_action_lengths[len_symbol]++;
			last_litrep = true;
		}
		else if(a.type == Action_Match){
			int len_symbol = symbol_from_int(a.length, min_match_len, NULL);

			assert(len_symbol < 32);
			if (last_litrep) {
				freqs_lengths_post_litrep[32+len_symbol]++;
			}
			else {
				freqs_action_lengths[32 + len_symbol]++;
			}
			
			//use separate huff tree here (we're implied if we get a match length)
			int distance_symbol = symbol_from_int(a.distance, 0, NULL);
			assert(distance_symbol < 32);
			freqs_distances[distance_symbol]++;
			last_litrep = false;
		}
		else if (a.type == Action_Rep0)
		{
			int len_symbol = symbol_from_int(a.length, 1, NULL);

			assert(len_symbol < 32);
			freqs_lengths_post_litrep[len_symbol]++;
			last_litrep = false;
		}
		cursor += a.length;
	}
	*literals = create_huff_tree(freqs_literals, 256);
	*distances = create_huff_tree(freqs_distances, 32);
	*action_lengths = create_huff_tree(freqs_action_lengths, 64);
	*lengths_post_litrep = create_huff_tree(freqs_lengths_post_litrep, 64);
}
#pragma optimize ("",on)


// pack the match results with huffman-trees
void* compress_actions(std::vector<Action> &actions, char *data, int *num_bytes){

	HuffTree action_lengths = {};
	HuffTree literals = {};
	HuffTree distances = {};
	HuffTree lengths_post_litrep = {};
	create_huff_trees(data, &literals, &distances, &action_lengths, &lengths_post_litrep, actions);
#if 0
	// this whole thing saves 12 fucking bits haha. Great.
	for (int i = 0; i < 10; i++)
	{
		for (int i = 0; i < results.size(); i++){
			maybe_convert_to_literal(&results[i], ht, ht_2);
		}
		create_huff_trees(&literals, &distances, &action_lengths, results); //@LEAK
	}
#endif

	int num_actions = actions.size();

	auto bw = make_bitwriter();
	bw.push_bits(num_actions, 32);

	
	literals.send(&bw);
	distances.send(&bw);
	action_lengths.send(&bw);
	lengths_post_litrep.send(&bw);

	bool last_litrep = false;
	int cursor = 0;
	for (Action a : actions){
		if (a.type == Action_Litrep){

			int len_bits=0;
			int len_symbol = symbol_from_int(a.length, 1, &len_bits);
			action_lengths.encode_symbol(&bw, len_symbol);
			bw.push_bits(len_bits, num_bits_from_symbol(len_symbol));

			for (int i = 0; i < a.length; i++){
				literals.encode_symbol(&bw, (uint8_t)data[i+cursor]);
			}
		}
		else if(a.type == Action_Match){

			for (int i = 0; i < a.length; i++) {
				assert(data[i + cursor] == data[i+cursor-a.distance]);
			}

			int distance_bits, len_bits;
			int len_symbol = symbol_from_int(a.length, min_match_len, &len_bits);
			int distance_symbol = symbol_from_int(a.distance, 0, &distance_bits);

			if (last_litrep){
				lengths_post_litrep.encode_symbol(&bw, len_symbol + 32);
				bw.push_bits(len_bits, num_bits_from_symbol(len_symbol));
			}
			else{
				action_lengths.encode_symbol(&bw, len_symbol + 32);
				bw.push_bits(len_bits, num_bits_from_symbol(len_symbol));
			}

			distances.encode_symbol(&bw, distance_symbol);
			bw.push_bits(distance_bits, num_bits_from_symbol(distance_symbol));
		}
		else if (a.type == Action_Rep0){
			int len_bits;
			int len_symbol = symbol_from_int(a.length, 1, &len_bits);
			lengths_post_litrep.encode_symbol(&bw, len_symbol);
			bw.push_bits(len_bits, num_bits_from_symbol(len_symbol));
		}
		else{
			assert(false);
		}


		last_litrep = a.type == Action_Litrep;
		cursor += a.length;

	}

	free_hufftree(action_lengths);
	free_hufftree(lengths_post_litrep);
	free_hufftree(distances);
	free_hufftree(literals);
	return bw.to_bytes(num_bytes);
}


void add_match(int distance, int length, std::vector<char> &decoded_data)
{
	for (int i = 0; i < length; i++) {
		char byte = decoded_data[decoded_data.size() - distance];
		decoded_data.push_back(byte);
	}
}
#pragma optimize ("", off)
void decompress(void *data, std::vector<char> &decoded_data) {
	
	auto br = make_bitreader(data);
	int num_actions = br.read_bits(32);


	HuffTree literals = create_huff_tree_from_br(&br, 256);
	HuffTree distances = create_huff_tree_from_br(&br, 32);
	HuffTree action_lengths = create_huff_tree_from_br(&br, 64);
	HuffTree lengths_post_litrep= create_huff_tree_from_br(&br, 64);

	bool last_litrep = false;
	int last_dist = -1;
	for (int i = 0; i < num_actions; i++){
		if (last_litrep){
			int symbol = lengths_post_litrep.decode_next_symbol(&br);
			bool is_rep0 = symbol < 32;
			symbol &= 31;
			int length, distance;
			if (is_rep0)
			{
				length = int_from_symbol(symbol, 1, br.read_bits(num_bits_from_symbol(symbol)));
				distance = last_dist;
			}
			else{
				length = int_from_symbol(symbol, min_match_len, br.read_bits(num_bits_from_symbol(symbol)));
				int symbol_2 = distances.decode_next_symbol(&br);
				distance = int_from_symbol(symbol_2, 0, br.read_bits(num_bits_from_symbol(symbol_2)));
			}

			add_match(distance, length, decoded_data);
			last_dist = distance;
			last_litrep = false;
		}
		else{
			int symbol = action_lengths.decode_next_symbol(&br);
			bool is_literal = symbol < 32;
			if (is_literal) {

				int len= int_from_symbol(symbol, 1, br.read_bits(num_bits_from_symbol(symbol)));
				for (int i = 0; i < len; i++){
					decoded_data.push_back(literals.decode_next_symbol(&br));
				}
				last_litrep = true;

			}
			else {
				symbol -= 32;
				int length = int_from_symbol(symbol, min_match_len, br.read_bits(num_bits_from_symbol(symbol)));
			
				int symbol_2 = distances.decode_next_symbol(&br);
				int distance = int_from_symbol(symbol_2, 0, br.read_bits(num_bits_from_symbol(symbol_2)));
				last_dist = distance;
				add_match(distance, length, decoded_data);
			}
		}
	}

	free_hufftree(action_lengths);
	free_hufftree(lengths_post_litrep);
	free_hufftree(distances);
	free_hufftree(literals);
}
#pragma optimize ("", on)

Action find_next_match(SuffixArray a, char *data, int cursor, int data_len)
{
	Action ret = { };


	auto match = a.find_longest_match(cursor);
	ret.length = match.length;
	ret.distance = cursor - match.start;

	if (ret.length < min_match_len)
	{
		ret.distance = 0; // play nice.
		ret.length = 1;
		ret.type = Action_Litrep;
	}

	return ret;
}

void *compress(char *data, int len, int *num_bytes)
{
	std::vector<Action> match_results;

	SuffixArray a = make_suffix_array(data, len);

	int previous_dist = -1;
	int cum_rep_len = 0;
	for(int i = 0; i<len;)
	{
#if 0
		if (previous_dist != -1)
		{
			int rep_len = a.num_chars_equal(i, i - previous_dist);
			if (rep_len > 1)
			{
				cum_rep_len += rep_len - 1;
				printf("rep len:%d | %d\n", rep_len, cum_rep_len);
			}
		}
		if (!match.is_literal) previous_dist = match.start;
#endif
		Action action = find_next_match(a, data, i, len);
		
		if (action.type == Action_Match){ 
			// check if we can encoding a better match at i+1.
			Action action_2 = find_next_match(a, data, i + 1, len);
			if (action_2.length > action.length + 1){
				action.length = 1;
				action.type = Action_Litrep;
			}
		}
		// coalessing the litreps.
		if (action.type == Action_Litrep && !match_results.empty() && match_results.back().type == Action_Litrep){
			match_results.back().length += action.length;
		}
		else{
			match_results.push_back(action);
		}
		i += action.length;
	}
	
	
	return compress_actions(match_results, data, num_bytes);
}

struct CostModel
{
	int cost_per_litrep = 2;
	int cost_per_match = 16;
	int cost_per_rep0 = 7;
};

// lzaa dynamic programming style.
// building up the 'cheapest' sequece of matches / litreps from reverse. 
// Using a simplified cost model.
// always only considers the longest closest match at a given positon. This is suboptimal but whatever. 
// this does not parse well when we use rep0's. And otherwise not great either.
// When encoding multiple matches in a row we want them to be the most scewed (ie one short and one long) 
// we don't do that here.
void *compress_slow(char *data, int len, int *num_bytes, CostModel cost_model = {}) {

	// the cost for encoding a literal is approx, log(1/p)
	static int costs_for_literal[256];
	memset(costs_for_literal, 0, sizeof(costs_for_literal));
	for (int i = 0; i < len; i++){
		costs_for_literal[(uint8_t)data[i]]++;
	}
	for (int i = 0; i < 256; i++) {
		if(costs_for_literal[i] != 0)
			costs_for_literal[i] = log2(len/costs_for_literal[i]); 
	}

	std::vector<Action> results;
	SuffixArray a = make_suffix_array(data, len);
	
	int *costs = (int *)calloc(len+1, sizeof(int)); // note the plus one here. costs[len] is allowed.
	Action *actions = (Action *)calloc(len+1, sizeof(Action)); // same here. it is not a litrep. (ie we cannot coaless with it.

	for (int i = len - 1; i >= 0; i--){
		Action match = find_next_match(a, data, i, len);
		Action litrep = {};
		litrep.type = Action_Litrep;
		int cost_litrep = INT_MAX;

		// search forward for optimal litrep length. 
		// we might want to 'over write' the next match since we don't need to bother with cost of the initial litrep to do so (only the cost for the literals)
		// the forward search saves a few bytes. not super worth it but whatever.
		int litereal_costs = 0;
		for (int j = 1; j < min(len - i + 1, 10); j++){
			litereal_costs += costs_for_literal[(uint8_t)data[i + j - 1]];
			if (actions[i + j].type == Action_Litrep) {
				int alt_cost = costs[i + j] + litereal_costs;
				if (alt_cost < cost_litrep) {
					cost_litrep = alt_cost;
					litrep.length = actions[i + j].length + j;
				}
				break; // the found litrep has continued the search so no need for us to do so.
			}
			else {
				int alt_cost = costs[i + j] + litereal_costs + cost_model.cost_per_litrep;
				if (alt_cost < cost_litrep) {
					cost_litrep = alt_cost;
					litrep.length = j;
				}
			}
		}

		int cost_match = INT_MAX;
		if(match.type == Action_Match){
			cost_match = costs[i + match.length] + cost_model.cost_per_match;
		}

		if (cost_match < cost_litrep && match.type == Action_Match){
			actions[i] = match;
			costs[i] = cost_match;
		}
		else{
			costs[i] = cost_litrep;
			actions[i] = litrep;
		}
	}

	int last_dist = -1;
	for(int i = 0; i < len;){
		Action action = actions[i];
		if (action.type == Action_Match){
			if (action.distance == last_dist) {
				action.type = Action_Rep0;
			}
			last_dist = action.distance;
		}
		results.push_back(action);
		i += action.length;
	}
	free(costs);
	free(actions);
	free_suffix_array(a);
	return compress_actions(results, data, num_bytes);
}

Action make_litrep_action(int length)
{
	Action ret;
	ret.length = length;
	ret.type = Action_Litrep;
	return ret;
}

Action make_match_action(int length, int distance)
{
	Action ret;
	ret.length = length;
	ret.distance = distance;
	ret.type = Action_Match;
	return ret;
}

Action make_rep0_action(int length, int distance) {
	Action ret;
	ret.length = length;
	ret.distance = distance;
	ret.type = Action_Rep0;
	return ret;
}


#pragma optimize("", off)
void *compress_forward_arrival(char *data, int len, int *num_bytes, CostModel cost_model = {}) {
	std::vector<Action> results;
	SuffixArray a = make_suffix_array(data, len);
	
	struct Arrival {
		Action action;
		uint32_t cost; // u so we can init to oxff to be max lol.
		Action rep0_history[2];
	};
	// lol what's the deal with this.
	// if we increase it we get worse results...
	// our cost model is worse than I though lol.
	const int num_arrivals = 4;

	typedef Arrival Arrivals[num_arrivals];
	Arrivals *arrivals = (Arrivals *)malloc((len + 1)* sizeof(Arrivals));
	memset(arrivals, 0xff, (len + 1) * sizeof(Arrivals));


	static int costs_for_literal[256];
	memset(costs_for_literal, 0, sizeof(costs_for_literal));
	for (int i = 0; i < len; i++) {
		costs_for_literal[(uint8_t)data[i]]++;
	}

	for (int i = 0; i < 256; i++) {
		if (costs_for_literal[i] != 0)
			costs_for_literal[i] = log2(len / costs_for_literal[i]);
	}

	auto set_min_arrival = [arrivals, num_arrivals, len](int i, Arrival b) {
		assert(i <= len);
		bool has_litrep = false;
		for (int j = 0; j < num_arrivals; j++) {
			if (arrivals[i][j].action.distance == b.action.distance &&
				arrivals[i][j].action.length == b.action.length &&
				arrivals[i][j].action.type == b.action.type)
			{
				if (arrivals[i][j].cost > b.cost) {
					arrivals[i][j] = b;
					assert(false);
				}
				return;
			}
		}
		for (int j = 0; j < num_arrivals; j++){
			if (arrivals[i][j].cost >= b.cost) {
				std::swap(arrivals[i][j], b);
			}
			has_litrep |= arrivals[i][j].action.type == Action_Litrep;
		}
		// make sure we always atleast one litrep around.
		if (!has_litrep && b.action.type == Action_Litrep){
			arrivals[i][num_arrivals - 1] = b;
		}
	};

	arrivals[0][0] = {};
	arrivals[0][0].action.type = Action_Rep0;

	for (int i = 0; i < len; i++) {
		for (int j = 0; j < num_arrivals; j++)
		{
			Action action = arrivals[i][j].action;
			int cost = arrivals[i][j].cost;
			if (cost == UINT32_MAX) break;
			
			if(j == 0){ // match
				Action match = find_next_match(a, data, i, len);
				if (match.type == Action_Match) {
					assert(match.distance <= i);
					assert(match.length >= min_match_len);

					Arrival arr = { match, cost + cost_model.cost_per_match };
					// why doesn't this make it better???
					//arr.cost += num_bits_from_symbol(symbol_from_int(match.length, min_match_len, 0));
					set_min_arrival(i + match.length, arr);
				}
			}

			{ //litrep
				Arrival litrep = {};
				litrep.action.type = Action_Litrep;
				litrep.cost = cost + costs_for_literal[(uint8_t)data[i]];
				if (action.type == Action_Litrep) {
					litrep.action.length = action.length + 1;
					litrep.cost += num_bits_from_symbol(symbol_from_int(action.length + 1, 1, 0)) - num_bits_from_symbol(symbol_from_int(action.length, 1, 0));
				}
				else {
					litrep.cost += cost_model.cost_per_litrep;
					litrep.action.length = 1;
				}
				set_min_arrival(i + 1, litrep);
			}
#if 1
			{ // rep0
				if (action.type == Action_Litrep)
				{
					Arrival rep0 = {};
					rep0.action.type = Action_Rep0;
					int prev_i = i - action.length;
					for(int q = 0; q< num_arrivals;q++){
						Arrival prev_arrival = arrivals[prev_i][q];
						if (prev_arrival.action.type == Action_Match){
							rep0.action.length = a.num_chars_equal(i, i-prev_arrival.action.distance);
							if (rep0.action.length > 0){
								rep0.cost = cost + cost_model.cost_per_rep0;
								rep0.action.distance = prev_arrival.action.distance;
								rep0.rep0_history[0] = action;
								rep0.rep0_history[1] = prev_arrival.action;
								set_min_arrival(i + rep0.action.length, rep0);
							}
						}
					}
				}
			}
#endif
		}
	}

	int num_rep0s= 0;
	int num_litreps = 0;
	int num_matches = 0;
	int len_reps = 0;
	int len_litreps = 0;
	int len_matches = 0;

	for (int i = len; i > 0; ) {
		Action arrival = arrivals[i][0].action;
		if (arrival.type == Action_Rep0) {
			Arrival arrival = arrivals[i][0];
			num_rep0s++;
			i -= arrival.action.length;
			i -= arrival.rep0_history[0].length;
			i -= arrival.rep0_history[1].length;

			results.push_back(arrival.action);
			results.push_back(arrival.rep0_history[0]);
			results.push_back(arrival.rep0_history[1]);

			len_litreps += arrival.rep0_history[0].length;
			len_matches += arrival.rep0_history[1].length;
			len_reps += arrival.action.length;
		}
		else{
			if (arrival.type == Action_Match) {
				++num_matches;
				results.push_back(arrival);
				len_matches += arrival.length;
			}
			else{
				if (!results.empty() && results.back().type == Action_Litrep){
					// if this happens our cost model is possitivly fucked!
					results.back().length += arrival.length;
					assert(false); 
				}
				else{
					results.push_back(arrival);
				}
				++num_litreps;
				len_litreps += arrival.length;
			}

			i -= arrival.length;
		}
	}

	float tot = (num_rep0s + num_matches + num_litreps)*0.01;
	float tot2 = (len_reps + len_matches + len_litreps)*0.01;

	printf("num_rep0s:%.3f%%\n", num_rep0s/tot);
	printf("num_matchess:%.3f%%\n", num_matches/tot);
	printf("num_litreps:%.3f%%\n", num_litreps/tot);

	printf("len_rep0s:%.3f%%\n", len_reps / tot2);
	printf("len_matchess:%.3f%%\n", len_matches / tot2);
	printf("len_litreps:%.3f%%\n", len_litreps / tot2);

	std::reverse(results.begin(), results.end());




	free(arrivals);
	free_suffix_array(a);
	return compress_actions(results, data, num_bytes);
}

#pragma optimize("", on)


char *read_file(const char *path)
{
	FILE *f;
	fopen_s(&f, path, "rb");
	fseek(f, 0, SEEK_END);
	long fsize = ftell(f);
	fseek(f, 0, SEEK_SET); 

	char *string = (char *)calloc(fsize + 1,1);
	fread(string, fsize, 1, f);
	fclose(f);
	return string;
}


int main()
{
#if 0
	{
		for(int i = 0;i< 32;i++)
			printf("%d->%d\n", i, symbol_from_int(i,0,NULL));
	}

	return 0;
#endif


#if 0
	char *data = (char*) "Lorem Ipsum is simply dummy text of the printing and typesetting industry. Lorem Ipsum has been the industry's standard dummy text ever since the 1500s, when an unknown printer took a galley of type and scrambled it to make a type specimen book. It has survived not only five centuries, but also the leap into electronic typesetting, remaining essentially unchanged. It was popularised in the 1960s with the release of Letraset sheets containing Lorem Ipsum passages, and more recently with desktop publishing software like Aldus PageMaker including versions of Lorem Ipsum"
		"It is a long established fact that a reader will be distracted by the readable content of a page when looking at its layout. The point of using Lorem Ipsum is that it has a more-or-less normal distribution of letters, as opposed to using 'Content here, content here', making it look like readable English. Many desktop publishing packages and web page editors now use Lorem Ipsum as their default model text, and a search for 'lorem ipsum' will uncover many web sites still in their infancy. Various versions have evolved over the years, sometimes by accident, sometimes on purpose (injected humour and the like)."
		"Contrary to popular belief, Lorem Ipsum is not simply random text. It has roots in a piece of classical Latin literature from 45 BC, making it over 2000 years old. Richard McClintock, a Latin professor at Hampden-Sydney College in Virginia, looked up one of the more obscure Latin words, consectetur, from a Lorem Ipsum passage, and going through the cites of the word in classical literature, discovered the undoubtable source. Lorem Ipsum comes from sections 1.10.32 and 1.10.33 of \"de Finibus Bonorum et Malorum\" (The Extremes of Good and Evil) by Cicero, written in 45 BC. This book is a treatise on the theory of ethics, very popular during the Renaissance. The first line of Lorem Ipsum, \"Lorem ipsum dolor sit amet..\", comes from a line in section 1.10.32.";
#elif 0 
	char *data = (char *) "Stand on the door. Stand in the door";
#else 1
#if 0
	const char *path = "C:\\Users\\Danie\\compression\\compression_project\\pride_and_predujudice.txt";
#else 
	const char *path = "C:\\Users\\Danie\\compression\\compression_project\\pg571.txt";
#endif
	char *data = read_file(path);
#endif
#if 0
	for (int cost_match = 1; cost_match < 20; cost_match++)
	{
		int min_num_bytes = INT_MAX;
		CostModel best_cost_modes;
		for (int cost_lrl = 1; cost_lrl < 20; cost_lrl++)
		{
			CostModel c = {};
			c.cost_per_match = cost_match;
			c.cost_per_litrep = cost_lrl;
			int num_data_bytes = strlen(data) + 1;
			int num_bytes;
			void *compressed_data = compress_forward_arrival(data, num_data_bytes, &num_bytes, c);
			if (num_bytes < min_num_bytes)
			{
				min_num_bytes = num_bytes;
				best_cost_modes = c;
			}
			printf(".");
		}
		printf("%d %d -> %d\n", best_cost_modes.cost_per_match,best_cost_modes.cost_per_litrep, min_num_bytes);
	}
#endif
	CostModel c = {};
	c.cost_per_litrep = 2;
	c.cost_per_match = 17;
	
	std::vector<char> decompressed_data;
	int num_data_bytes = strlen(data) + 1;
	int num_bytes;
	void *compressed_data = compress_forward_arrival(data, num_data_bytes, &num_bytes,c);
	decompress(compressed_data, decompressed_data);

	
	printf("FA Compressed %d bytes of data to %d bytes\n", num_data_bytes, num_bytes);
	printf("FA Compression Ratio of: %fx\n", num_data_bytes/ (float)num_bytes);




	for (int i = 0; i < num_data_bytes; i++) {

		if (data[i] != decompressed_data[i]){
			printf("error at %d\n", i);
		}
		assert(data[i] == decompressed_data[i]);
	}

	{
		void *compressed_data = compress_slow(data, num_data_bytes, &num_bytes);
		decompress(compressed_data, decompressed_data);


		printf("Slow Compressed %d bytes of data to %d bytes\n", num_data_bytes, num_bytes);
		printf("Slow Compression Ratio of: %fx\n", num_data_bytes / (float)num_bytes);
	}

	getchar();
}

