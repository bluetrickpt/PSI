/*
 * naive-psi.cpp
 *
 *  Created on: Jul 9, 2014
 *      Author: mzohner
 */
#include "naive-psi.h"


uint32_t naivepsi(role_type role, uint32_t neles, uint32_t pneles, uint32_t elebytelen, uint8_t* elements,
		uint8_t** result, crypto* crypt_env, CSocket* sock, uint32_t ntasks) {

	uint32_t i, intersect_size, maskbytelen;
	task_ctx_naive ectx;
	CSocket* tmpsock = sock;

	uint32_t* perm;
	uint8_t *permeles, *hashes, *phashes;

	maskbytelen = ceil_divide(crypt_env->get_seclvl().statbits + ceil_log2(neles) + ceil_log2(pneles), 8);

	permeles = (uint8_t*) malloc(sizeof(uint8_t) * neles * elebytelen);
	hashes = (uint8_t*) malloc(sizeof(uint8_t) * neles * maskbytelen);
	perm  = (uint32_t*) malloc(sizeof(uint32_t) * neles);


	/* Permute the elements */
	crypt_env->gen_rnd_perm(perm, neles);
	for(i = 0; i < neles; i++) {
		memcpy(permeles + perm[i] * elebytelen,  elements + i * elebytelen, elebytelen);
	}

	/* Hash elements */
#ifdef DEBUG
	cout << "Hashing my elements" << endl;
#endif

	ectx.eles.input = permeles;
	ectx.eles.inbytelen = elebytelen;
	ectx.eles.outbytelen = maskbytelen,
	ectx.eles.nelements = neles;
	ectx.eles.output = hashes;
	ectx.hctx.symcrypt = crypt_env;

	run_task_naive(ntasks, ectx, hash_naive);

	phashes = (uint8_t*) malloc(sizeof(uint8_t) * pneles * maskbytelen);


#ifdef DEBUG
	cout << "Exchanging hashes" << endl;
#endif
	snd_and_rcv_naive(hashes, neles * maskbytelen, phashes, pneles * maskbytelen, tmpsock);

	/*cout << "Hashes of my elements: " << endl;
	for(i = 0; i < neles; i++) {
		for(uint32_t j = 0; j < maskbytelen; j++) {
			cout << (hex) << (uint32_t) hashes[i * maskbytelen + j] << (dec);
		}
		cout << endl;
	}*/

	/*cout << "Hashes of partner elements: " << endl;
	for(i = 0; i < npeles; i++) {
		for(uint32_t j = 0; j < hash_bytes; j++) {
			cout << (hex) << (uint32_t) phashes[i * hash_bytes + j] << (dec);
		}
		cout << endl;
	}*/
#ifdef DEBUG
	cout << "Finding intersection" << endl;
#endif
	intersect_size = find_intersection_naive(elements, result, elebytelen, hashes,
			neles, phashes, pneles, maskbytelen, perm);


#ifdef DEBUG
	cout << "Free-ing allocated memory" << endl;
#endif
	free(perm);
	free(hashes);
	free(permeles);
	free(phashes);

	return intersect_size;
}



uint32_t find_intersection_naive(uint8_t* elements, uint8_t** result, uint32_t elebytelen, uint8_t* hashes,
		uint32_t neles, uint8_t* phashes, uint32_t pneles, uint32_t hashbytelen, uint32_t* perm) {

	uint32_t* invperm = (uint32_t*) malloc(sizeof(uint32_t) * neles);
	uint32_t* matches = (uint32_t*) malloc(sizeof(uint32_t) * neles);
	uint64_t* tmpval;

	uint32_t size_intersect, i, intersect_ctr;

	for(i = 0; i < neles; i++) {
		invperm[perm[i]] = i;
	}
	//cout << "My number of elements. " << neles << ", partner number of elements: " << pneles << ", maskbytelen: " << hashbytelen << endl;

	GHashTable *map= g_hash_table_new_full(g_int64_hash, g_int64_equal, NULL, NULL);
	for(i = 0; i < neles; i++) {
		g_hash_table_insert(map,(void*) ((uint64_t*) &(hashes[i*hashbytelen])), &(invperm[i]));
	}

	//for(i = 0; i < pneles; i++) {
	//	((uint64_t*) &(phashes[i*hashbytelen]))[0]++;
	//}

	for(i = 0, intersect_ctr = 0; i < pneles; i++) {

		if(g_hash_table_lookup_extended(map, (void*) ((uint64_t*) &(phashes[i*hashbytelen])),
		    				NULL, (void**) &tmpval)) {
			matches[intersect_ctr] = tmpval[0];
			intersect_ctr++;
			//assert(intersect_ctr <= min(neles, pneles));
		}
	}

	size_intersect = intersect_ctr;

	//result = (uint8_t**) malloc(sizeof(uint8_t*));
	(*result) = (uint8_t*) malloc(sizeof(uint8_t) * size_intersect * elebytelen);
	for(i = 0; i < size_intersect; i++) {
		memcpy((*result) + i * elebytelen, elements + matches[i] * elebytelen, elebytelen);
	}

	free(invperm);
	free(matches);
	return size_intersect;
}

void snd_and_rcv_naive(uint8_t* snd_buf, uint32_t snd_bytes, uint8_t* rcv_buf, uint32_t rcv_bytes, CSocket* sock) {
	pthread_t snd_task;
	bool created, joined;
	snd_ctx_naive ctx;

	//Start new sender thread
	ctx.sock = sock;
	ctx.snd_buf = snd_buf;
	ctx.snd_bytes = snd_bytes;
	created = !pthread_create(&snd_task, NULL, send_data_naive, (void*) &(ctx));

	//receive
	sock->Receive(rcv_buf, rcv_bytes);
	assert(created);

	joined = !pthread_join(snd_task, NULL);
	assert(joined);

}

void run_task_naive(uint32_t nthreads, task_ctx_naive context, void* (*func)(void*) ) {
	task_ctx_naive* contexts = (task_ctx_naive*) malloc(sizeof(task_ctx_naive) * nthreads);
	pthread_t* threads = (pthread_t*) malloc(sizeof(pthread_t) * nthreads);
	uint32_t i, neles_thread, electr, neles_cur;
	bool created, joined;

	neles_thread = ceil_divide(context.eles.nelements, nthreads);
	for(i = 0, electr = 0; i < nthreads; i++) {
		neles_cur = min(context.eles.nelements - electr, neles_thread);
		memcpy(contexts + i, &context, sizeof(task_ctx_naive));
		contexts[i].eles.nelements = neles_cur;
		contexts[i].eles.input = context.eles.input + (context.eles.inbytelen * electr);
		contexts[i].eles.output = context.eles.output + (context.eles.outbytelen * electr);
		electr += neles_cur;
	}

	for(i = 0; i < nthreads; i++) {
		created = !pthread_create(threads + i, NULL, func, (void*) &(contexts[i]));
	}

	assert(created);

	for(i = 0; i < nthreads; i++) {
		joined = !pthread_join(threads[i], NULL);
	}

	assert(joined);

	free(threads);
	free(contexts);
}

void *hash_naive(void* context) {
#ifdef DEBUG
	cout << "Hashing thread started" << endl;
#endif
	crypto* crypt_env = ((task_ctx_naive*) context)->hctx.symcrypt;
	element_ctx_naive electx = ((task_ctx_naive*) context)->eles;

	uint8_t *inptr=electx.input, *outptr=electx.output;
	uint32_t i;

	for(i = 0; i < electx.nelements; i++, inptr+=electx.inbytelen, outptr+=electx.outbytelen) {
		crypt_env->hash(outptr, electx.outbytelen, inptr, electx.inbytelen);
	}
	return 0;
}

void *send_data_naive(void* context) {
	snd_ctx_naive *ctx = (snd_ctx_naive*) context;
	ctx->sock->Send(ctx->snd_buf, ctx->snd_bytes);
	return 0;
}




void print_naive_psi_usage() {
	cout << "Usage: ./naivepsi [0 (server)/1 (client)] [num_elements] " <<
			"[element_byte_length] [sym_security_bits] [server_ip] [server_port]" << endl;
	cout << "Program exiting" << endl;
	exit(0);
}