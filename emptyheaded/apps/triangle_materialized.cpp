#include "main.hpp"

template<class T>
class triangle_materialized: public application<T> {
  void run(std::string path){
    //create the relation (currently a column wise table)
    Relation<uint64_t,uint64_t> *R_ab = new Relation<uint64_t,uint64_t>();

//////////////////////////////////////////////////////////////////////
    //File IO (for a tsv, csv should be roughly the same)
    auto rt = debug::start_clock();
    tsv_reader f_reader(path);
    // tsv_reader f_reader(dataset);

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
    allocator::memory<uint8_t> output_buffer(R_ab->num_rows * sizeof(uint64_t) * sizeof(Block<hybrid>));

    auto qt = debug::start_clock();

    const Head<hybrid> H = *TR_ab->head;
    const Set<hybrid> A = H.data;

    size_t tid = 0;
    auto head_t = debug::start_clock();
    Head<hybrid>* a_block = (Head<hybrid>*)output_buffer.get_next(tid, sizeof(Head<hybrid>));
    a_block->data = A;
    a_block->map = new Block<hybrid>*[A.cardinality + 1]; // TODO: is this always guaranteed to be at least the maximum value??
    // a_block->map = (Block<hybrid>**)block_buffer.get_next(tid, sizeof(Block<hybrid>*) * (A.cardinality + 1));
    debug::stop_clock("Build Head", head_t);

    A.foreach([&](uint32_t a_i){
      const Set<hybrid> matching_b = ((Tail<hybrid>*) H.get_block(a_i))->data;
      
      Block<hybrid>* b_block = (Block<hybrid>*)output_buffer.get_next(tid, sizeof(Block<hybrid>));
      //Block<hybrid>* b_block = new Block<hybrid>();
      // TODO: how to compute number of bytes??

      const size_t alloc_size = sizeof(uint64_t)*R_ab->num_rows;
      b_block->data = output_buffer.get_next(tid, alloc_size);
      ops::set_intersect(&b_block->data, &matching_b, &A); //intersect the B
      output_buffer.roll_back(tid, alloc_size - b_block->data.number_of_bytes);

      a_block->set_block(a_i,b_block);

      b_block->data.foreach([&](uint32_t b_i){ // Peel off B attributes
        const Tail<hybrid>* matching_c = (Tail<hybrid>*)H.get_block(b_i);
        if(matching_c != NULL){
          // Placement new!!
          Tail<hybrid>* C_block_ptr = (Tail<hybrid>*)output_buffer.get_next(tid, sizeof(Tail<hybrid>));
          C_block_ptr->data = Set<hybrid>(output_buffer.get_next(tid, alloc_size));

          ops::set_intersect(&C_block_ptr->data, &matching_c->data, &matching_b);
          output_buffer.roll_back(tid, alloc_size - C_block_ptr->data.number_of_bytes);
          b_block->set_block(b_i,(Block<hybrid>*)C_block_ptr);
        }
      });
    });

    debug::stop_clock("Query",qt);

    unsigned long size = 0;
    a_block->data.foreach([&](uint32_t a_i) {
        Block<hybrid>* b_block = a_block->get_block(a_i);
        if (b_block) {
          b_block->data.foreach([&](uint32_t b_i) {
              Block<hybrid>* c_block = b_block->get_block(b_i);
              if (c_block) {
                size += c_block->data.cardinality;
              }
          });
        }
      });

    std::cout << size << std::endl;
   //////////////////////////////////////////////////////////////////////
  }
};

template<class T>
application<T>* init_app(){
  return new triangle_materialized<T>();
}
