#ifndef TRISYCL_SYCL_VENDOR_XILINX_ACAP_ME_ARRAY_HPP
#define TRISYCL_SYCL_VENDOR_XILINX_ACAP_ME_ARRAY_HPP

/** \file

    Model of a MathEngine array

    Ronan at Keryell point FR

    This file is distributed under the University of Illinois Open Source
    License. See LICENSE.TXT for details.
*/

#include <iostream>
#include <thread>

#include "geography.hpp"

namespace cl::sycl::vendor::xilinx::acap::me {

/** The MathEngine array structure
 */
template <typename Layout,
          template <typename Geography,
                    typename ME_Array,
                    typename SuperTile,
                    int X,
                    int Y> typename Tile,
          template <typename Geography,
                    typename ME_Array,
                    int X,
                    int Y> typename Memory>
struct array {

  using geo = geography<Layout>;

  template <typename TileMemory>
  struct tile {
    using memory_t = TileMemory;
     memory_t *memory;

    void set_memory(memory_t &m) {
      memory = &m;
    }

    auto &mem() {
      return *memory;
    }
  };


  /** The pipes for the cascade streams, with 1 spare pipe on each
      side of PE strings

      \todo Use a data type with 384 bits

      There are 4 registers along the data path according to 1.4
      specification. */
  cl::sycl::static_pipe<int, 4>
  cascade_stream_pipes[geo::x_size*geo::y_size + 1];

  template <int X, int Y>
  using tileable_memory = Memory<geo, array, X, Y>;
  /// All the memory module of the ME array.
  /// Unfortunately it is not possible to use auto here...
  // Otherwise static inline auto
  decltype(geo::template generate_tiles<tileable_memory>()) memories =
    geo::template generate_tiles<tileable_memory>();

  template <int X, int Y>
  using tileable_tile = Tile<geo, array, tile<tileable_memory<X, Y>>, X, Y>;
  /// All the tiles of the ME array.
  /// Unfortunately it is not possible to use auto here...
  // Otherwise static inline auto
  decltype(geo::template generate_tiles<tileable_tile>()) tiles =
    geo::template generate_tiles<tileable_tile>();

#if 0
  template <int X, int Y>
  auto get_tile() {
    return boost::hana::find_if(
        tiles
      , [] (auto& tile) {
          return true;// tile.x == x && tile.y == y;
        }
                                );
  }
#endif


  /** Cascade stream layout

      On even rows, a tile use cascade_stream_pipes[y][x] as input and
      cascade_stream_pipes[y][x + 1] as output

      On odd rows the flow goes into the other direction, so a tile
      use cascade_stream_pipes[y][x + 1] as input and
      cascade_stream_pipes[y][x] as output


  */
  auto& get_cascade_stream_in(int x, int y) {
    // On odd rows, the cascade streams goes in the other direction
    return cascade_stream_pipes[geo::x_size*y
                                + ((y & 1) ? geo::x_max - x : x)];
  }


  auto get_cascade_stream_out(int x, int y) {
    // On odd rows, the cascade streams goes in the other direction
    return cascade_stream_pipes[geo::x_size*y + 1
                                + ((y & 1) ? geo::x_max - x : x)];
  }


  void run() {
    boost::hana::for_each(tiles, [&] (auto& t) {
        t.set_array(this);
        t.set_memory(boost::hana::at_c<t.template get_linear_id()>
                     (memories));
      });

    boost::hana::for_each(tiles, [&] (auto& t) {
        t.thread = std::thread {[&] {
            TRISYCL_DUMP_T("Starting ME tile (" << t.x << ',' << t.y
                           << ") linear id = " << t.get_linear_id());
            t.run(*this);
            TRISYCL_DUMP_T("Stopping ME tile (" << t.x << ',' << t.y << ')');
          }
        };
      });

    boost::hana::for_each(tiles, [&] (auto& t) {
        TRISYCL_DUMP_T("Joining ME tile (" << t.x << ',' << t.y << ')');
        t.thread.join();
        TRISYCL_DUMP_T("Joined ME tile (" << t.x << ',' << t.y << ')');
      });

    std::cout << "Total size of the tiles: " << sizeof(tiles)
              << " bytes." << std::endl;
  }
};

}

/*
    # Some Emacs stuff:
    ### Local Variables:
    ### ispell-local-dictionary: "american"
    ### eval: (flyspell-prog-mode)
    ### End:
*/

#endif // TRISYCL_SYCL_VENDOR_XILINX_ACAP_ME_ARRAY_HPP
