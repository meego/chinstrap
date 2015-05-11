#include <cmath>
#include <iostream>
#include <unordered_map>
#include "emptyheaded.hpp"
#include "utils/io.hpp"


extern "C" void run(std::unordered_map<std::string, void*>& relations, const char* dataset) {

    //create the relation (currently a column wise table)
    Relation<uint64_t,uint64_t> *R_ab = new Relation<uint64_t,uint64_t>();

//////////////////////////////////////////////////////////////////////
    //File IO (for a tsv, csv should be roughly the same)
    auto rt = debug::start_clock();
    //tsv_reader f_reader("data/small.txt");
    tsv_reader f_reader(std::string("/dfs/scratch0/caberger/datasets/") + dataset + "/edgelist/replicated.tsv");

    char *next = f_reader.tsv_get_first();
    R_ab->num_rows = 0;
    while(next != NULL){
      //have to code generate number of attributes here
      //maybe can accomplish with variadic templates? Might be hard.
      R_ab->get<0>().append_from_string(next);
      next = f_reader.tsv_get_next();
      R_ab->get<1>().append_from_string(next);
      next = f_reader.tsv_get_next();
      R_ab->num_rows++;
    }
    debug::stop_clock("Reading File",rt);
//////////////////////////////////////////////////////////////////////
    //Encoding
    auto et = debug::start_clock();
    //encode A
    std::vector<Column<uint64_t>> *a_attributes = new std::vector<Column<uint64_t>>();
    a_attributes->push_back(R_ab->get<0>());
    a_attributes->push_back(R_ab->get<1>());
    Encoding<uint64_t> *a_encoding = new Encoding<uint64_t>(a_attributes);
    debug::stop_clock("Encoding",et);
//////////////////////////////////////////////////////////////////////
    //Trie building

    //after all encodings are done, set up encoded relation (what is passed into the Trie)
    //You can switch the ordering here to be what you want it to be in the Trie.
    //A mapping will need to be kept in the query compiler so it is known what
    //encoding each level in the Trie should map to.

    auto bt = debug::start_clock();
    std::vector<Column<uint32_t>> *ER_ab = new std::vector<Column<uint32_t>>();
    ER_ab->push_back(a_encoding->encoded.at(0)); //perform filter, selection
    ER_ab->push_back(a_encoding->encoded.at(1));

    //add some sort of lambda to do selections
    Trie<hybrid> *TR_ab = Trie<hybrid>::build(ER_ab,[&](size_t index){
      return ER_ab->at(0).at(index) > ER_ab->at(1).at(index);
    });

    debug::stop_clock("Build",bt);

//////////////////////////////////////////////////////////////////////
    //Prints the relation
    //R(a,b) join T(b,c) join S(a,c)

    //allocate memory
    allocator::memory<uint8_t> A_buffer((pow(R_ab->num_rows, 1.5) + 1000) * sizeof(uint32_t));
    allocator::memory<uint8_t> B_buffer((pow(R_ab->num_rows, 1.5) + 1000) * sizeof(uint32_t));
    allocator::memory<uint8_t> C_buffer((pow(R_ab->num_rows, 1.5) + 1000) * sizeof(uint32_t));

    typedef std::unordered_map<uint32_t, Block<hybrid>*> map_t;
    auto qt = debug::start_clock();

    const Head<hybrid> H = *TR_ab->head;
    const Set<hybrid> A = H.data;

    auto head_t = debug::start_clock();
    Head<hybrid>* a_block = new Head<hybrid>();
    a_block->data.data = A_buffer.get_next(0, A.cardinality * sizeof(uint32_t));
    memcpy(a_block->data.data, A.data, A.number_of_bytes);
    a_block->data.cardinality = A.cardinality;
    a_block->data.number_of_bytes = A.number_of_bytes;
    a_block->data.density = A.density;
    a_block->data.type = A.type;

    a_block->map = new Block<hybrid>*[A.cardinality];
    debug::stop_clock("Build Head", head_t);

    A.par_foreach([&](size_t tid, uint32_t a_i){
      const Set<hybrid> op1 = ((Tail<hybrid>*) H.get_block(a_i))->data;
      Block<hybrid>* b_block = new Block<hybrid>();
      b_block->data.data = B_buffer.get_next(tid, std::min(A.cardinality * sizeof(uint32_t), op1.cardinality * sizeof(uint32_t)));

      a_block->map[a_i] = b_block;

      ops::set_intersect(&b_block->data,&op1,&A); //intersect the B

      op1.foreach([&](uint32_t b_i){ // Peel off B attributes
        const Tail<hybrid>* l2 = (Tail<hybrid>*)H.get_block(b_i);
        if(l2 != NULL){
          Set<hybrid>* C_values = new Set<hybrid>(C_buffer.get_next(tid, std::min(l2->data.cardinality * sizeof(uint32_t), op1.cardinality * sizeof(uint32_t))));
          ops::set_intersect(C_values, &l2->data, &op1);
          b_block->map[b_i] = (Block<hybrid>*)C_values;
        }
      });
    });

    debug::stop_clock("Query",qt);

    size_t size = 0;
    a_block->data.foreach([&](uint32_t a_i) {
        Block<hybrid>* b_block = a_block->map[a_i];
        if (b_block) {
          b_block->data.foreach([&](uint32_t b_i) {
              Block<hybrid>* c_block = b_block->map[b_i];
              if (c_block) {
                std::cout << "a: " << a_i
                          << "\tb: " << b_i
                          << "\tcardinality: "
                          << c_block->data.cardinality
                          << std::endl;

                size += c_block->data.cardinality;
              }
          });
        }
      });

    std::cout << size << std::endl;
   //////////////////////////////////////////////////////////////////////
}

int main(int argc, char* argv[]) {
  std::unordered_map<std::string, void*> relations;
  run(relations, argv[1]);
}
