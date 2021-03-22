#include <stdio.h>
#include "custom_unistd.h"
#include <pthread.h>
#include <string.h>
#define malloc(_size) heap_malloc_debug((_size), __LINE__, __FILE__)
#define calloc(_number, _size) heap_calloc_debug((_number), (_size), __LINE__, __FILE__)
#define realloc(_ptr, _size) heap_realloc_debug((_ptr), (_size), __LINE__, __FILE__)
#define malloc_aligned(_size) heap_malloc_aligned_debug((_size), __LINE__, __FILE__)
#define calloc_aligned(_number, _size) heap_calloc_aligned_debug((_number), (_size), __LINE__, __FILE__)
#define realloc_aligned(_ptr, _size) heap_realloc_aligned_debug((_ptr), (_size), __LINE__, __FILE__)
#define META_SIZE 64
#define PAGE_SIZE 4096

void* thread_test(void* arg) {
    int num = *(int *)arg;
    void *ptr = NULL;
    switch(num) {
        case 0:
            ptr = malloc(500);
            break;
        case 1:
            ptr = malloc_aligned(500 + 1);
            break;
        case 2:
            ptr = calloc_aligned(500 + 2, sizeof(char));
            break;
        case 3:
            ptr = malloc(5000 + 5);
    }
    return ptr;
}

void* thread_free(void* arg) {
    heap_free(arg);
    return NULL;
}

int main(int argc, char **argv)
{
    //TESTOWANE SA TYLKO FUNKCJE Z RODZINY _DEBUG PONIEWAZ ICH DZIALANIE JEST ZASADNICZO IDENTYCZNE
    printf("1. Test funkcji heap_setup\n");
    assert(heap_setup() == 0); //sterta poprawna
    printf("OK\n\n");

    printf("2. Test funkcji heap_malloc_debug - dzialanie poprawne\n");
    void *ptr1;
    ptr1 = malloc(5000);
    assert(ptr1 != NULL); //malloc musi sie udac
    struct block_meta *meta = (struct block_meta *)((intptr_t)ptr1 - META_SIZE);
    assert(heap_get_block_size(ptr1) == 5000); //poprawna wielkosc bloku
    printf("OK\n\n");

    printf("3. Test funkcji heap_malloc_debug - size == 0\n");
    void *ptr2;
    ptr2 = malloc(0);
    assert(ptr2 == NULL); //malloc nie moze sie udac
    ptr2 = malloc(1024 * 1024 * 80);
    assert(ptr2 == NULL); //malloc nie moze sie udac
    printf("OK\n\n");

    printf("4. Test funkcji heap_calloc_debug - dzialanie poprawne\n");
    void *ptr3;
    ptr3 = calloc(5000, sizeof(char));
    assert(ptr3 != NULL); //malloc musi sie udac
    char temp[5000];
    memset(temp, 0, 5000);
    assert(memcmp(ptr3, temp, 5000) == 0); //dane musza byc wyzerowane
    printf("OK\n\n");

    printf("5. Test funkcji heap_realloc_debug - dzialanie poprawne\n");
    void *ptr4;
    ptr4 = realloc(ptr1, 500);
    assert(ptr4 != NULL); //malloc musi sie udac
    assert(heap_get_block_size(ptr4) == 500); //nowy rozmiar musi byc poprawny
    printf("OK\n\n");

    printf("6. Test funkcji heap_realloc_debug - size == 0 -> free()\n");
    ptr1 = realloc(ptr4, 0);
    assert(get_pointer_type(ptr4) == pointer_unallocated); //blok musi byc pusty
    printf("OK\n\n");

    printf("7. Test funkcji heap_realloc_debug - ptr == NULL -> malloc()\n");
    ptr2 = realloc(NULL, 5000);
    assert(ptr2 != NULL); //malloc musi sie udac
    printf("OK\n\n");

    printf("8. Test funkcji heap_free\n");
    heap_free(ptr1);
    heap_free(ptr2);
    heap_free(ptr3);
    heap_free(ptr4);
    assert(heap_get_used_space() == META_SIZE); //na stercie nie moze byc zadnych zaalakowanych blokow
    printf("OK\n\n");

    printf("9. Test funkcji heap_malloc_aligned_debug - dzialanie poprawne\n");
    ptr1 = malloc_aligned(3500);
    assert(ptr1 != NULL); //malloc musi sie udac
    assert(((intptr_t)ptr1 & (intptr_t)(PAGE_SIZE - 1)) == 0); //wskaznik musi byc na poczatku strony
    ptr2 = malloc_aligned(0);
    assert(ptr2 == NULL); //malloc nie moze sie udac
    heap_free(ptr1);
    printf("OK\n\n");

    printf("10. Test funkcji heap_calloc_aligned_debug - dzialanie poprawne\n");
    ptr1 = calloc_aligned(5000, sizeof(char));
    assert(ptr1 != NULL); //malloc musi sie udac
    assert(((intptr_t)ptr1 & (intptr_t)(PAGE_SIZE - 1)) == 0); //wskaznik musi byc na poczatku strony
    assert(memcmp(ptr1, temp, 5000) == 0); //dane musza byc wyzerowane
    printf("OK\n\n");

    printf("11. Test funkcji heap_realloc_aligned_debug - dzialanie poprawne\n");
    ptr4 = realloc_aligned(ptr1, 500);
    assert(ptr4 != NULL); //malloc musi sie udac
    assert(heap_get_block_size(ptr4) == 500); //nowy rozmiar musi byc poprawny
    assert(((intptr_t)ptr4 & (intptr_t)(PAGE_SIZE - 1)) == 0); //wskaznik musi byc na poczatku strony
    printf("OK\n\n");

    printf("12. Test funkcji heap_realloc_aligned_debug - size == 0 -> free()\n");
    ptr1 = realloc_aligned(ptr4, 0);
    assert(get_pointer_type(ptr4) != pointer_valid); //blok musi byc pusty lub nie istniec(optymalizacja free)
    printf("OK\n\n");

    printf("13. Test funkcji heap_realloc_aligned_debug - ptr == NULL -> malloc()\n");
    ptr2 = realloc_aligned(NULL, 5000);
    assert(ptr2 != NULL); //malloc musi sie udac
    assert(((intptr_t)ptr2 & (intptr_t)(PAGE_SIZE - 1)) == 0); //wskaznik musi byc na poczatku strony
    printf("OK\n\n");


    /*
    * Obecny stan sterty:
    * Block address: 000000000040F040, size: 3968 - PUSTY
    * Block address: 0000000000410000, size: 5000 - zaalokowany alligned dla ptr2
    * Block address: 00000000004113C8, size: 3128 - PUSTY
    */

    printf("14. Test funkcji heap_get_used_space\n");
    assert(heap_get_used_space() == 3 * META_SIZE + 5000);
    printf("OK\n\n");

    printf("15. Test funkcji heap_get_largest_used_block_size\n");
    assert(heap_get_largest_used_block_size() == 5000);
    printf("OK\n\n");

    printf("16. Test funkcji heap_get_used_blocks_count\n");
    assert(heap_get_used_blocks_count() == 1);
    printf("OK\n\n");

    printf("17. Test funkcji heap_get_free_space\n");
    assert(heap_get_free_space() == 3968 + 3128);
    printf("OK\n\n");

    printf("18. Test funkcji heap_get_largest_free_area\n");
    assert(heap_get_largest_free_area() == 3968);
    printf("OK\n\n");

    printf("19. Test funkcji heap_get_free_gaps_count\n");
    assert(heap_get_free_gaps_count() == 2);
    printf("OK\n\n");

    printf("20. Test funkcji get_pointer_type\n");
    assert(get_pointer_type(NULL) == pointer_null); //wskaznik na null
    assert(get_pointer_type((void *)((intptr_t)ptr2 + 50000)) == pointer_out_of_heap); //pewien dowolny wskaznik wskazujacy poza sterte
    assert(get_pointer_type((void *)((intptr_t)ptr2 - META_SIZE)) == pointer_control_block); //wskaznik na blok kontrolny
    assert(get_pointer_type((void *)((intptr_t)ptr2 + 50)) == pointer_inside_data_block); //wskaznik gdzies wewnatrz bloku z danymi
    ptr1 = malloc(50);
    heap_free(ptr1);
    assert(get_pointer_type(ptr1) == pointer_unallocated); //wskaznik na niezaalokowany blok
    assert(get_pointer_type(ptr2) == pointer_valid); //poprawny wskaznik na poczatek danych
    printf("OK\n\n");

    printf("21. Test funkcji heap_get_data_block_start\n");
    assert(heap_get_data_block_start((void *)((intptr_t)ptr2 + 50)) == ptr2); //funkcja musi przesunac wskaznik na poczatek bloku danych
    printf("OK\n\n");

    printf("22. Test funkcji heap_get_block_size\n");
    assert(heap_get_block_size(ptr2) == 5000); //funkcja musi zwrocic poprawna wielkosc bloku danych
    printf("OK\n\n");

    printf("23. Test funkcji heap_dump_debug_information\n");
    heap_dump_debug_information();
    //poprawnosc do oceny przez sprawdzajacego
    printf("OK\n\n");

    printf("24. Test funkcji heap_validate\n");
    assert(heap_validate() == 0); //sterta poprawna
    printf("OK\n\n");

    printf("25. Test alokatora w warunkach wielowatkowosci\n");
    pthread_t threads[4];
    void *p[4];
    int param[4] = {0, 1, 2, 3};
    printf("BEFORE:\n");
    heap_dump_debug_information();
    for(int i = 0; i < 4; ++i)
        pthread_create(&threads[i], NULL, thread_test, (void*)(param + i)); //tworzymy watki alokujace pamiec
    for(int i = 0; i < 4; ++i)
        pthread_join(threads[i], &p[i]);
    assert(heap_get_used_blocks_count() == 4 + 1); //liczba blokow musi sie zgadzac
    printf("AFTER:\n");
    heap_dump_debug_information();
    for(int i = 0; i < 4; ++i)
        pthread_create(&threads[i], NULL, thread_free, p[i]); //tworzymy watki zwalniajace przydzielona wczesniej pamiec
    for(int i = 0; i < 4; ++i)
        pthread_join(threads[i], NULL);
    assert(heap_get_used_blocks_count() == 1); //liczba blokow musi sie zgadzac
    printf("OK\n\n");

    //TESTY USZKADZAJACE STERTE:
    printf("26. Test funkcji heap_validate\n");
    memcpy(temp, ptr1, 5000); //kopia zapasowa 5000 bajtow od 1 pustego bloku
    memset(ptr1, 0, 5000); //naruszenie struktury sterty - nadpisanie metadanych, plotkow i wyjscie poza zakres bloku
    assert(heap_validate() != 0); //sterta musi byc uszkodzona
    memcpy(ptr1, temp, 5000); //przywracamy prawidlowy uklad sterty
    printf("OK\n\n");

    printf("27. Test funkcji heap_validate\n");
    meta = (struct block_meta *)((intptr_t)ptr2 - META_SIZE);
    memcpy(temp, meta, 5000); //backup
    meta->start_fence = 0; //niszczymy plotek struktury
    assert(heap_validate() == -3); //uszkodzony plotek struktury
    memcpy(meta, temp, 5000);
    printf("OK\n\n");

    printf("28. Test funkcji heap_validate\n");
    meta->next = ptr1; //uszkadzamy wskaznik na nastepny blok
    assert(heap_validate() == -1); //zly wskaznik
    memcpy(meta, temp, 5000);
    printf("OK\n\n");

    printf("29. Test funkcji heap_validate\n");
    meta = (struct block_meta *)((intptr_t)ptr1 - 5000);
    memcpy(temp, meta, 3000);
    memset(meta, 0, 3000); //uszkadzamy plotki sterty;
    assert(heap_validate() == -2); //plotki sterty uszkodzone
    printf("OK\n\n");

    printf("31. Test funkcji heap_setup - sterta uszkodzona i nie mozna jej zresetowac (heap != NULL)\n");
    assert(heap_setup() != 0);
    printf("OK\n\n");

    printf("32. Test funkcji heap_setup - reset sterty (heap != NULL)\n");
    memcpy(meta, temp, 3000); //przywracamy poprawnosc sterty z poprzedniego testu
    assert(heap_setup() == 0); //organizujemy sterte of nowa
    assert(heap_get_used_space() == META_SIZE); //nowa sterta musi byc poprawna
    printf("OK\n\n");
}

#if 0 //PASSED
#include "custom_unistd.h"

int main(int argc, char **argv) {
	int status = heap_setup();
	assert(status == 0);

	// parametry pustej sterty
	size_t free_bytes = heap_get_free_space();
	size_t used_bytes = heap_get_used_space();
	void* p1 = heap_malloc(8 * 1024 * 1024); // 8MB
	void* p2 = heap_malloc(8 * 1024 * 1024); // 8MB
	void* p3 = heap_malloc(8 * 1024 * 1024); // 8MB
	void* p4 = heap_malloc(45 * 1024 * 1024); // 45MB
	assert(p1 != NULL); // malloc musi się udać
	assert(p2 != NULL); // malloc musi się udać
	assert(p3 != NULL); // malloc musi się udać
	assert(p4 == NULL); // nie ma prawa zadziałać
    // Ostatnia alokacja, na 45MB nie może się powieść,
    // ponieważ sterta nie może być aż tak
    // wielka (brak pamięci w systemie operacyjnym).
    heap_dump_debug_information();
	status = heap_validate();
	assert(status == 0); // sterta nie może być uszkodzona

	// zaalokowano 3 bloki
	assert(heap_get_used_blocks_count() == 3);

	// zajęto 24MB sterty; te 2000 bajtów powinno
    // wystarczyć na wewnętrzne struktury sterty
	assert(
        heap_get_used_space() >= 24 * 1024 * 1024 &&
        heap_get_used_space() <= 24 * 1024 * 1024 + 2000
        );

	// zwolnij pamięć
	heap_free(p1);
	heap_free(p2);
	heap_free(p3);

	// wszystko powinno wrócić do normy
	assert(heap_get_free_space() == free_bytes);
	assert(heap_get_used_space() == used_bytes);

	// już nie ma bloków
	assert(heap_get_used_blocks_count() == 0);

	return 0;
}
#endif