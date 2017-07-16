/*----------------------------------------------------------------------*
 * File:    kmer_stats.c                                                *
 * Purpose: Handle calculation and display of stats                     *
 * Author:  Richard Leggett                                             *
 *          Ricardo Ramirez-Gonzalez                                    *
 *          The Genome Analysis Centre (TGAC), Norwich, UK              *
 *          richard.leggett@tgac.ac.uk    								*
 *----------------------------------------------------------------------*/

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <getopt.h>
#include <ctype.h>
#include <math.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include "global.h"
#include "binary_kmer.h"
#include "element.h"
#include "hash_table.h"
#include "cmd_line.h"
#include "kmer_stats.h"
#include "kmer_reader.h"

/*----------------------------------------------------------------------*
 * Function:
 * Purpose:
 * Parameters: None
 * Returns:    None
 *----------------------------------------------------------------------*/
void kmer_stats_read_counts_initialise(KmerStatsReadCounts *r)
{
    int i;
    
    pthread_mutex_init(&(r->lock), NULL);
    r->number_of_reads = 0;
    r->k1_contaminated_reads = 0;
    r->kn_contaminated_reads = 0;
    
    for (i=0; i<MAX_CONTAMINANTS; i++) {
        r->contaminant_kmers_seen[i] = 0;
        r->k1_contaminated_reads_by_contaminant[i] = 0;
        r->k1_unique_contaminated_reads_by_contaminant[i] = 0;
        r->species_read_counts[i] = 0;
    }
    
    for (i=0; i<MAX_READ_LENGTH; i++) {
        r->contaminated_kmers_per_read[i] = 0;
    }
    
    r->species_unclassified = 0;
}

/*----------------------------------------------------------------------*
 * Function:
 * Purpose:
 * Parameters: None
 * Returns:    None
 *----------------------------------------------------------------------*/
void kmer_stats_both_reads_initialise(KmerStatsBothReads *r)
{
    int i;
    
    pthread_mutex_init(&(r->lock), NULL);
                       
    r->number_of_reads = 0;

    r->threshold_passed_reads = 0;
    r->k1_both_reads_not_threshold = 0;
    r->k1_either_read_not_threshold = 0;
    r->threshold_passed_reads_unique = 0;
    r->k1_both_reads_not_threshold_unique = 0;
    r->k1_either_read_not_threshold_unique = 0;
    
    for (i=0; i<MAX_CONTAMINANTS; i++) {
        r->threshold_passed_reads_by_contaminant[i] = 0;
        r->k1_both_reads_not_threshold_by_contaminant[i] = 0;
        r->k1_either_read_not_threshold_by_contaminant[i] = 0;
        r->threshold_passed_reads_unique_by_contaminant[i] = 0;
        r->k1_both_reads_not_threshold_unique_by_contaminant[i] = 0;
        r->k1_either_read_not_threshold_unique_by_contaminant[i] = 0;
    }
    
    r->filter_read = false;
}

/*----------------------------------------------------------------------*
 * Function:
 * Purpose:
 * Parameters: None
 * Returns:    None
 *----------------------------------------------------------------------*/
void kmer_stats_initialise(KmerStats* stats, CmdLine* cmd_line)
{
    int i;
    int j;
    int r;

    pthread_mutex_init(&(stats->lock), NULL);
                       
    stats->n_contaminants = 0;
    stats->number_of_files = 0;
    
    for (i=0; i<MAX_CONTAMINANTS; i++) {
        stats->contaminant_kmers[i] = 0;
    }

    for (i=0; i<MAX_CONTAMINANTS; i++) {
        stats->unique_kmers[i] = 0;
        for (j=0; j<MAX_CONTAMINANTS; j++) {
            stats->kmers_in_common[i][j] = 0;
        }
    }
    
    for (r=0; r<2; r++) {
        stats->read[r] = calloc(1, sizeof(KmerStatsReadCounts));
        if (stats->read[r] == 0) {
            printf("Error: Can't allocate space for KmerStatsReadCounts!\n");
            exit(2);
        }
        kmer_stats_read_counts_initialise(stats->read[r]);
    }
    
    stats->both_reads = calloc(1, sizeof(KmerStatsBothReads));
    if (stats->both_reads == 0) {
        printf("Error: Can't allocate space for KmerStatsBothReads!\n");
        exit(2);
    }
    kmer_stats_both_reads_initialise(stats->both_reads);
}


/*----------------------------------------------------------------------*
 * Function:   update_stats_parallel
 * Purpose:    Update overall read stats from a KmerCounts read structure
 * Parameters: None
 * Returns:    None
 *----------------------------------------------------------------------*/
void update_stats_parallel(int r, KmerCounts* counts, KmerStats* stats, CmdLine* cmd_line)
{
    int i;
    int largest_contaminant = 0;
    int largest_kmers = 0;
    int unique_largest_contaminant = 0;
    int unique_largest_kmers = 0;
    
    pthread_mutex_lock(&(stats->read[r]->lock));
    // Update number of reads
    stats->read[r]->number_of_reads++;
    // We allow up to the maximum read length. The last element is the cumulative of the reads/contigs that have more than the space we have allocated
    stats->read[r]->contaminated_kmers_per_read[counts->kmers_loaded < MAX_READ_LENGTH? counts->kmers_loaded:MAX_READ_LENGTH]++;
    pthread_mutex_unlock(&(stats->read[r]->lock));
    
    if (counts->kmers_loaded > 0) {
        // Go through all contaminants
        for (i=0; i<stats->n_contaminants; i++) {
            // If we got a kmer...
            if (counts->kmers_from_contaminant[i] > 0) {
                // Have we got more kmers for this contaminant than for any others?
                if (counts->kmers_from_contaminant[i] > largest_kmers) {
                    largest_kmers = counts->kmers_from_contaminant[i];
                    largest_contaminant = i;
                }
                
                // Have we got more unique kmers for this contaminant than for any others?
                if (counts->unique_kmers_from_contaminant[i] > unique_largest_kmers) {
                    unique_largest_kmers = counts->unique_kmers_from_contaminant[i];
                    unique_largest_contaminant = i;
                }
                
                // Update the count of contaminanted reads
                pthread_mutex_lock(&(stats->read[r]->lock));
                stats->read[r]->k1_contaminated_reads_by_contaminant[i]++;
                pthread_mutex_unlock(&(stats->read[r]->lock));
                
                // If only one contaminant detected, then we can safely update the number of k1 unique reads
                if (counts->contaminants_detected == 1) {
                    pthread_mutex_lock(&(stats->read[r]->lock));
                    stats->read[r]->k1_unique_contaminated_reads_by_contaminant[i]++;
                    pthread_mutex_unlock(&(stats->read[r]->lock));
                }
            }
        }
        
        // Update number of k1 (not necessarily unique) reads
        pthread_mutex_lock(&(stats->read[r]->lock));
        stats->read[r]->k1_contaminated_reads++;
        pthread_mutex_unlock(&(stats->read[r]->lock));
    }
    
    // If we didn't find any kmers, then this is unclassified
    if (largest_kmers == 0) {
        pthread_mutex_lock(&(stats->read[r]->lock));
        stats->read[r]->reads_unclassified++;
        pthread_mutex_unlock(&(stats->read[r]->lock));
        counts->assigned_contaminant = -1;
    } else {
        // But if we did, store the higest contaminant (the "assigned" contaminant)
        pthread_mutex_lock(&(stats->read[r]->lock));
        stats->read[r]->reads_with_highest_contaminant[largest_contaminant]++;
        pthread_mutex_unlock(&(stats->read[r]->lock));
        counts->assigned_contaminant = largest_contaminant;
    }
    
    // If we didn't find any unqique kmers, then...
    if (unique_largest_kmers == 0) {
        counts->unique_assigned_contaminant = -1;
    } else {
        // But if we did...
        counts->unique_assigned_contaminant = unique_largest_contaminant;
    }
    
    // If we got over our single read kmer threshold...
    if (counts->kmers_loaded >= cmd_line->kmer_threshold_read) {
        for (i=0; i<stats->n_contaminants; i++) {
            if (counts->kmers_from_contaminant[i] > cmd_line->kmer_threshold_read) {
                pthread_mutex_lock(&(stats->read[r]->lock));
                stats->read[r]->kn_contaminated_reads_by_contaminant[i]++;
                pthread_mutex_unlock(&(stats->read[r]->lock));
                if (counts->contaminants_detected == 1) {
                    pthread_mutex_lock(&(stats->read[r]->lock));
                    stats->read[r]->kn_unique_contaminated_reads_by_contaminant[i]++;
                    pthread_mutex_unlock(&(stats->read[r]->lock));
                }
            }
        }
        
        pthread_mutex_lock(&(stats->read[r]->lock));
        stats->read[r]->kn_contaminated_reads++;
        pthread_mutex_unlock(&(stats->read[r]->lock));
    }
}


/*----------------------------------------------------------------------*
 * Function:   update_stats
 * Purpose:    Update overall read stats from a KmerCounts read structure
 * Parameters: None
 * Returns:    None
 *----------------------------------------------------------------------*/
void update_stats(int r, KmerCounts* counts, KmerStats* stats, CmdLine* cmd_line)
{
    int i;
    int largest_contaminant = 0;
    int largest_kmers = 0;
    int unique_largest_contaminant = 0;
    int unique_largest_kmers = 0;
    
    stats->read[r]->number_of_reads++;
    
    // We allow up to the maximum read length. The last element is the cumulative of the reads/contigs that have more than the space we have allocated
    stats->read[r]->contaminated_kmers_per_read[counts->kmers_loaded < MAX_READ_LENGTH? counts->kmers_loaded:MAX_READ_LENGTH]++;
    
    if (counts->kmers_loaded > 0) {
        // Go through all contaminants
        for (i=0; i<stats->n_contaminants; i++) {
            // If we got a kmer...
            if (counts->kmers_from_contaminant[i] > 0) {
                // Have we got more kmers for this contaminant than for any others?
                if (counts->kmers_from_contaminant[i] > largest_kmers) {
                    largest_kmers = counts->kmers_from_contaminant[i];
                    largest_contaminant = i;
                }

                // Have we got more unique kmers for this contaminant than for any others?
                if (counts->unique_kmers_from_contaminant[i] > unique_largest_kmers) {
                    unique_largest_kmers = counts->unique_kmers_from_contaminant[i];
                    unique_largest_contaminant = i;
                }
                
                // Update the count of contaminanted reads
                stats->read[r]->k1_contaminated_reads_by_contaminant[i]++;
                
                // If only one contaminant detected, then we can safely update the number of k1 unique reads
                if (counts->contaminants_detected == 1) {
                    stats->read[r]->k1_unique_contaminated_reads_by_contaminant[i]++;
                }
            }
        }
        
        // Update number of k1 (not necessarily unique) reads
        stats->read[r]->k1_contaminated_reads++;
    }
    
    // If we didn't find any kmers, then this is unclassified
    if (largest_kmers == 0) {
        stats->read[r]->reads_unclassified++;
        counts->assigned_contaminant = -1;
    } else {
        // But if we did, store the higest contaminant (the "assigned" contaminant)
        stats->read[r]->reads_with_highest_contaminant[largest_contaminant]++;
        counts->assigned_contaminant = largest_contaminant;
    }
    
    // If we didn't find any unqique kmers, then...
    if (unique_largest_kmers == 0) {
        counts->unique_assigned_contaminant = -1;
    } else {
        // But if we did...
        counts->unique_assigned_contaminant = unique_largest_contaminant;
    }
    
    // If we got over our single read kmer threshold...
    if (counts->kmers_loaded >= cmd_line->kmer_threshold_read) {
        for (i=0; i<stats->n_contaminants; i++) {
            if (counts->kmers_from_contaminant[i] > cmd_line->kmer_threshold_read) {
                stats->read[r]->kn_contaminated_reads_by_contaminant[i]++;
                if (counts->contaminants_detected == 1) {
                    stats->read[r]->kn_unique_contaminated_reads_by_contaminant[i]++;
                }
            }
        }
        
        stats->read[r]->kn_contaminated_reads++;
    }
}



/*----------------------------------------------------------------------*
 * Function:
 * Purpose:
 * Parameters: None
 * Returns:    None
 *----------------------------------------------------------------------*/
boolean update_stats_for_both_parallel(KmerStats* stats, CmdLine* cmd_line, KmerCounts* counts_a, KmerCounts* counts_b)
{
    boolean threshold_met = false;
    boolean unique_threshold_met = false;
    int i;
    int largest_contaminant = 0;
    int largest_kmers = 0;
    int one_in_both = 0;
    int one_in_either = 0;
    int unique_largest_contaminant = 0;
    int unique_largest_kmers = 0;
    int unique_one_in_both = 0;
    int unique_one_in_either = 0;
    boolean filter_read = false;
    
    // Go through all contaminants and find best match for this pair
    for (i=0; i<stats->n_contaminants; i++) {
        // First for ALL kmers... after that for unique kmers
        int a = counts_a->kmers_from_contaminant[i];
        int b = counts_b->kmers_from_contaminant[i];
        int t = a + b;
        
        // If we got a kmer for this contaminant...
        if ((a >= cmd_line->kmer_threshold_read) &&
            (b >= cmd_line->kmer_threshold_read) &&
            (t >= cmd_line->kmer_threshold_overall)) {
            // It meets our thresholds. Is it the best yet?
            if (t > largest_kmers) {
                largest_kmers = t;
                largest_contaminant = i;
            }
            threshold_met = true;
        } else if (!threshold_met) {
            if ((a >= 1) && (b >= 1)) {
                // One or more in both
                one_in_both++;
                if (t > largest_kmers) {
                    largest_kmers  = t;
                    largest_contaminant = i;
                }
            } else if (((a >= 1) && (b == 0)) || ((a == 0) && (b >= 1))) {
                // One or more in A or B
                one_in_either++;
                if (one_in_both == 0) {
                    if (t > largest_kmers) {
                        largest_kmers  = t;
                        largest_contaminant = i;
                    }
                }
            }
        }
        
        // Now the unique kmers
        a = counts_a->unique_kmers_from_contaminant[i];
        b = counts_b->unique_kmers_from_contaminant[i];
        t = a + b;
        
        // If we got a kmer for this contaminant...
        if ((a >= cmd_line->kmer_threshold_read) &&
            (b >= cmd_line->kmer_threshold_read) &&
            (t >= cmd_line->kmer_threshold_overall)) {
            // It meets our thresholds. Is it the best yet?
            if (t > unique_largest_kmers) {
                unique_largest_kmers = t;
                unique_largest_contaminant = i;
            }
            unique_threshold_met = true;
        } else if (!unique_threshold_met) {
            if ((a >= 1) && (b >= 1)) {
                // One or more in both
                unique_one_in_both++;
                if (t > unique_largest_kmers) {
                    unique_largest_kmers = t;
                    unique_largest_contaminant = i;
                }
            } else if (((a >= 1) && (b == 0)) || ((a == 0) && (b >= 1))) {
                // One or more in A or B
                unique_one_in_either++;
                if (unique_one_in_both == 0) {
                    if (t > unique_largest_kmers) {
                        unique_largest_kmers = t;
                        unique_largest_contaminant = i;
                    }
                }
            }
        }
    }
    
    // Update read counts
    if (threshold_met) {
        pthread_mutex_lock(&(stats->both_reads->lock));
        stats->both_reads->threshold_passed_reads++;
        stats->both_reads->threshold_passed_reads_by_contaminant[largest_contaminant]++;
        pthread_mutex_unlock(&(stats->both_reads->lock));
        if (cmd_line->filter_unique == false) {
            filter_read = true;
        }
    } else if (one_in_both > 0) {
        pthread_mutex_lock(&(stats->both_reads->lock));
        stats->both_reads->k1_both_reads_not_threshold++;
        stats->both_reads->k1_both_reads_not_threshold_by_contaminant[largest_contaminant]++;
        pthread_mutex_unlock(&(stats->both_reads->lock));
    } else if (one_in_either > 0) {
        pthread_mutex_lock(&(stats->both_reads->lock));
        stats->both_reads->k1_either_read_not_threshold++;
        stats->both_reads->k1_either_read_not_threshold_by_contaminant[largest_contaminant]++;
        pthread_mutex_unlock(&(stats->both_reads->lock));
    }
    
    if (unique_threshold_met) {
        pthread_mutex_lock(&(stats->both_reads->lock));
        stats->both_reads->threshold_passed_reads_unique++;
        stats->both_reads->threshold_passed_reads_unique_by_contaminant[unique_largest_contaminant]++;
        pthread_mutex_unlock(&(stats->both_reads->lock));
        filter_read = true;
    } else if (unique_one_in_both > 0) {
        pthread_mutex_lock(&(stats->both_reads->lock));
        stats->both_reads->k1_both_reads_not_threshold_unique++;
        stats->both_reads->k1_both_reads_not_threshold_unique_by_contaminant[unique_largest_contaminant]++;
        pthread_mutex_unlock(&(stats->both_reads->lock));
    } else if (unique_one_in_either > 0) {
        pthread_mutex_lock(&(stats->both_reads->lock));
        stats->both_reads->k1_either_read_not_threshold_unique++;
        stats->both_reads->k1_either_read_not_threshold_unique_by_contaminant[unique_largest_contaminant]++;
        pthread_mutex_unlock(&(stats->both_reads->lock));
    }
    
    return filter_read;
}



/*----------------------------------------------------------------------*
 * Function:
 * Purpose:
 * Parameters: None
 * Returns:    None
 *----------------------------------------------------------------------*/
boolean update_stats_for_both(KmerStats* stats, CmdLine* cmd_line, KmerCounts* counts_a, KmerCounts* counts_b)
{
    boolean threshold_met = false;
    boolean unique_threshold_met = false;
    int i;
    int largest_contaminant = 0;
    int largest_kmers = 0;
    int one_in_both = 0;
    int one_in_either = 0;
    int unique_largest_contaminant = 0;
    int unique_largest_kmers = 0;
    int unique_one_in_both = 0;
    int unique_one_in_either = 0;
    boolean filter_read = false;
    
    // Go through all contaminants and find best match for this pair
    for (i=0; i<stats->n_contaminants; i++) {
        // First for ALL kmers... after that for unique kmers
        int a = counts_a->kmers_from_contaminant[i];
        int b = counts_b->kmers_from_contaminant[i];
        int t = a + b;
        
        // If we got a kmer for this contaminant...
        if ((a >= cmd_line->kmer_threshold_read) &&
            (b >= cmd_line->kmer_threshold_read) &&
            (t >= cmd_line->kmer_threshold_overall)) {
            // It meets our thresholds. Is it the best yet?
            if (t > largest_kmers) {
                largest_kmers = t;
                largest_contaminant = i;
            }
            threshold_met = true;
        } else if (!threshold_met) {
            if ((a >= 1) && (b >= 1)) {
                // One or more in both
                one_in_both++;
                if (t > largest_kmers) {
                    largest_kmers  = t;
                    largest_contaminant = i;
                }
            } else if (((a >= 1) && (b == 0)) || ((a == 0) && (b >= 1))) {
                // One or more in A or B
                one_in_either++;
                if (one_in_both == 0) {
                    if (t > largest_kmers) {
                        largest_kmers  = t;
                        largest_contaminant = i;
                    }
                }
            }
        }
        
        // Now the unique kmers
        a = counts_a->unique_kmers_from_contaminant[i];
        b = counts_b->unique_kmers_from_contaminant[i];
        t = a + b;
        
        // If we got a kmer for this contaminant...
        if ((a >= cmd_line->kmer_threshold_read) &&
            (b >= cmd_line->kmer_threshold_read) &&
            (t >= cmd_line->kmer_threshold_overall)) {
            // It meets our thresholds. Is it the best yet?
            if (t > unique_largest_kmers) {
                unique_largest_kmers = t;
                unique_largest_contaminant = i;
            }
            unique_threshold_met = true;
        } else if (!unique_threshold_met) {
            if ((a >= 1) && (b >= 1)) {
                // One or more in both
                unique_one_in_both++;
                if (t > unique_largest_kmers) {
                    unique_largest_kmers = t;
                    unique_largest_contaminant = i;
                }
            } else if (((a >= 1) && (b == 0)) || ((a == 0) && (b >= 1))) {
                // One or more in A or B
                unique_one_in_either++;
                if (unique_one_in_both == 0) {
                    if (t > unique_largest_kmers) {
                        unique_largest_kmers = t;
                        unique_largest_contaminant = i;
                    }
                }
            }
        }
    }
    
    // Update read counts
    if (threshold_met) {
        stats->both_reads->threshold_passed_reads++;
        stats->both_reads->threshold_passed_reads_by_contaminant[largest_contaminant]++;
        if (cmd_line->filter_unique == false) {
            filter_read = true;
        }
    } else if (one_in_both > 0) {
        stats->both_reads->k1_both_reads_not_threshold++;
        stats->both_reads->k1_both_reads_not_threshold_by_contaminant[largest_contaminant]++;
    } else if (one_in_either > 0) {
        stats->both_reads->k1_either_read_not_threshold++;
        stats->both_reads->k1_either_read_not_threshold_by_contaminant[largest_contaminant]++;
    }

    if (unique_threshold_met) {
        stats->both_reads->threshold_passed_reads_unique++;
        stats->both_reads->threshold_passed_reads_unique_by_contaminant[unique_largest_contaminant]++;
        filter_read = true;
    } else if (unique_one_in_both > 0) {
        stats->both_reads->k1_both_reads_not_threshold_unique++;
        stats->both_reads->k1_both_reads_not_threshold_unique_by_contaminant[unique_largest_contaminant]++;
    } else if (unique_one_in_either > 0) {
        stats->both_reads->k1_either_read_not_threshold_unique++;
        stats->both_reads->k1_either_read_not_threshold_unique_by_contaminant[unique_largest_contaminant]++;
    }
    
    return filter_read;
}



/*----------------------------------------------------------------------*
 * Function:
 * Purpose:
 * Parameters: None
 * Returns:    None
 *----------------------------------------------------------------------*/
void kmer_stats_calculate_read(KmerStats* stats, KmerStatsReadCounts* read)
{
    int i;
    
    read->k1_contaminaned_reads_pc = (100.0 * (double)read->k1_contaminated_reads) / (double)read->number_of_reads;
    read->kn_contaminaned_reads_pc = (100.0 * (double)read->kn_contaminated_reads) / (double)read->number_of_reads;
    
    for (i=0; i<stats->n_contaminants; i++) {
        read->k1_contaminated_reads_by_contaminant_pc[i] = (100.0 * (double)read->k1_contaminated_reads_by_contaminant[i]) / (double)read->number_of_reads;
        read->k1_unique_contaminated_reads_by_contaminant_pc[i] = (100.0 * (double)read->k1_unique_contaminated_reads_by_contaminant[i]) / (double)read->number_of_reads;
        read->kn_contaminated_reads_by_contaminant_pc[i] = (100.0 * (double)read->kn_contaminated_reads_by_contaminant[i]) / (double)read->number_of_reads;
        read->kn_unique_contaminated_reads_by_contaminant_pc[i] = (100.0 * (double)read->kn_unique_contaminated_reads_by_contaminant[i]) / (double)read->number_of_reads;
        read->contaminant_kmers_seen_pc[i] = (100.0 * (double)read->contaminant_kmers_seen[i]) / (double)stats->contaminant_kmers[i];
        
        if (read->species_read_counts[i] > 0) {
            read->species_read_counts_pc[i] = (100.0 * (double)read->species_read_counts[i] / read->number_of_reads);
        } else {
            read->species_read_counts_pc[i] = 0;
        }
        
        if (read->species_unclassified > 0) {
            read->species_unclassified_pc = (100.0 * (double)read->species_unclassified / read->number_of_reads);
        } else {
            read->species_unclassified = 0;
        }
    }
}

/*----------------------------------------------------------------------*
 * Function:
 * Purpose:
 * Parameters: None
 * Returns:    None
 *----------------------------------------------------------------------*/
void kmer_stats_calculate_both(KmerStats* stats)
{
    KmerStatsBothReads* b = stats->both_reads;
    int i;
    
    b->threshold_passed_reads_pc = (100.0 * (double)b->threshold_passed_reads) / (double)b->number_of_reads;
    b->k1_both_reads_not_threshold_pc = (100.0 * (double)b->k1_both_reads_not_threshold) / (double)b->number_of_reads;
    b->k1_either_read_not_threshold_pc = (100.0 * (double)b->k1_either_read_not_threshold) / (double)b->number_of_reads;
    b->threshold_passed_reads_pc_unique = (100.0 * (double)b->threshold_passed_reads_unique) / (double)b->number_of_reads;
    b->k1_both_reads_not_threshold_pc_unique = (100.0 * (double)b->k1_both_reads_not_threshold_unique) / (double)b->number_of_reads;
    b->k1_either_read_not_threshold_pc_unique = (100.0 * (double)b->k1_either_read_not_threshold_unique) / (double)b->number_of_reads;
    
    for (i=0; i<stats->n_contaminants; i++) {
        b->contaminant_kmers_seen_pc[i] = (100.0 * (double)b->contaminant_kmers_seen[i]) / (double)stats->contaminant_kmers[i];
        
        b->threshold_passed_reads_by_contaminant_pc[i] = (100.0 * (double)b->threshold_passed_reads_by_contaminant[i]) / (double)b->number_of_reads;
        b->k1_both_reads_not_threshold_by_contaminant_pc[i] = (100.0 * (double)b->k1_both_reads_not_threshold_by_contaminant[i]) / (double)b->number_of_reads;
        b->k1_either_read_not_threshold_by_contaminant_pc[i] = (100.0 * (double)b->k1_either_read_not_threshold_by_contaminant[i]) / (double)b->number_of_reads;
        b->threshold_passed_reads_unique_by_contaminant_pc[i] = (100.0 * (double)b->threshold_passed_reads_unique_by_contaminant[i]) / (double)b->number_of_reads;
        b->k1_both_reads_not_threshold_unique_by_contaminant_pc[i] = (100.0 * (double)b->k1_both_reads_not_threshold_unique_by_contaminant[i]) / (double)b->number_of_reads;
        b->k1_either_read_not_threshold_unique_by_contaminant_pc[i] = (100.0 * (double)b->k1_either_read_not_threshold_unique_by_contaminant[i]) / (double)b->number_of_reads;
    }
}


/*----------------------------------------------------------------------*
 * Function:
 * Purpose:
 * Parameters: None
 * Returns:    None
 *----------------------------------------------------------------------*/
void kmer_stats_calculate(KmerStats* stats)
{
    kmer_stats_calculate_read(stats, stats->read[0]);
    kmer_stats_calculate_read(stats, stats->read[1]);
    kmer_stats_calculate_both(stats);
 }

/*----------------------------------------------------------------------*
 * Function:
 * Purpose:
 * Parameters: None
 * Returns:    None
 *----------------------------------------------------------------------*/
void kmer_stats_report_read_stats(KmerStats* stats, int r, CmdLine* cmd_line)
{
    int i;
    char str[1024];
    KmerStatsReadCounts* read = stats->read[r];
    
    printf("Overall statistics\n\n");
    printf("%64s: %d\n", "Number of reads", read->number_of_reads);
    printf("%64s: %d\t%.2f %%\n", "Number of reads with 1+ kmer contamination", read->k1_contaminated_reads, read->k1_contaminaned_reads_pc);
    
    if (cmd_line->kmer_threshold_read != 1) {
        sprintf(str, "Number of reads with %d+ kmer contamination", cmd_line->kmer_threshold_read);
        printf("%64s: %d\t%.2f %%\n", str, read->kn_contaminated_reads, read->kn_contaminaned_reads_pc);
    }
    
    printf("\nPer-contaminant statistics\n\n");
    
    printf("%-30s %-10s %-10s %-10s %-10s %-10s %-10s %-10s %-10s %-10s %-10s %-10s %-10s %-10s\n", "Contaminant", "nKmers", "kFound", "%kFound", "ReadsW1k", "%ReadsW1k", "UniqW1k", "%UniqW1k", "ReadsWnk", "%ReadsWnk", "UniqWnk", "%UniqWnk", "Assigned", "%Assigned");
           
    for (i=0; i<stats->n_contaminants; i++) {
        printf("%-30s %-10u %-10u %-10.2f %-10u %-10.2f %-10u %-10.2f %-10u %-10.2f %-10u %-10.2f %-10u %-10.2f\n",
               stats->contaminant_ids[i],
               stats->contaminant_kmers[i],
               read->contaminant_kmers_seen[i],
               read->contaminant_kmers_seen_pc[i],
               read->k1_contaminated_reads_by_contaminant[i],
               read->k1_contaminated_reads_by_contaminant_pc[i],
               read->k1_unique_contaminated_reads_by_contaminant[i],
               read->k1_unique_contaminated_reads_by_contaminant_pc[i],
               read->kn_contaminated_reads_by_contaminant[i],
               read->kn_contaminated_reads_by_contaminant_pc[i],
               read->kn_unique_contaminated_reads_by_contaminant[i],
               read->kn_unique_contaminated_reads_by_contaminant_pc[i],
               read->species_read_counts[i],
               read->species_read_counts_pc[i]
               );
    }
    printf("%-30s %-10s %-10s %-10s %-10s %-10s %-10s %-10s %-10s %-10s %-10s %-10s %-10u %-10.2f\n", "Unclassified", "", "", "", "", "", "", "", "", "", "", "", read->species_unclassified, read->species_unclassified_pc);
}

/*----------------------------------------------------------------------*
 * Function:
 * Purpose:
 * Parameters: None
 * Returns:    None
 *----------------------------------------------------------------------*/
void kmer_stats_report_both_stats(KmerStats* stats, CmdLine* cmd_line)
{
    int i;
    
    printf("Overall statistics\n\n");
    printf("%64s: %d\n\n", "Number of pairs", stats->both_reads->number_of_reads);
    printf("%64s: %d\t%.2f %%\n", "Reads meeting threshold (all kmers)", stats->both_reads->threshold_passed_reads, stats->both_reads->threshold_passed_reads_pc);
    printf("%64s: %d\t%.2f %%\n", "Remaining reads with at least 1 kmer in each", stats->both_reads->k1_both_reads_not_threshold, stats->both_reads->k1_both_reads_not_threshold_pc);
    printf("%64s: %d\t%.2f %%\n\n", "Remaining reads with at least 1 kmer in either", stats->both_reads->k1_either_read_not_threshold, stats->both_reads->k1_either_read_not_threshold_pc);

    printf("%64s: %d\t%.2f %%\n", "Reads meeting threshold (unique kmers only)", stats->both_reads->threshold_passed_reads_unique, stats->both_reads->threshold_passed_reads_pc_unique);
    printf("%64s: %d\t%.2f %%\n", "Remaining reads with at least 1 unique kmer in each", stats->both_reads->k1_both_reads_not_threshold_unique, stats->both_reads->k1_both_reads_not_threshold_pc_unique);
    printf("%64s: %d\t%.2f %%\n", "Remaining reads with at least 1 unique kmer in either", stats->both_reads->k1_either_read_not_threshold_unique, stats->both_reads->k1_either_read_not_threshold_pc_unique);

    
    printf("\nPer-contaminant statistics\n\n");

    printf("%-30s %-10s %-10s %-10s %-10s %-10s %-10s %-10s %-10s %-10s %-10s %-10s %-10s %-10s %-10s %-10s\n", "Contaminant", "nKmers", "kFound", "%%kFound", "ReadsThr", "%%ReadsThr", "BothW1k", "%%BothW1k", "EithW1k", "%%Eith1k", "UniqRTh", "%%UniqRTh", "UniqB1k", "%%UniqB1k", "UniqE1k", "%%UniqE1k");
    
    for (i=0; i<stats->n_contaminants; i++) {
        printf("%-30s %-10u %-10u %-10.2f %-10u %-10.2f %-10u %-10.2f %-10u %-10.2f %-10u %-10.2f %-10u %-10.2f %-10u %-10.2f\n",
               stats->contaminant_ids[i],
               stats->contaminant_kmers[i],
               stats->both_reads->contaminant_kmers_seen[i],
               stats->both_reads->contaminant_kmers_seen_pc[i],
               stats->both_reads->threshold_passed_reads_by_contaminant[i],
               stats->both_reads->threshold_passed_reads_by_contaminant_pc[i],
               stats->both_reads->k1_both_reads_not_threshold_by_contaminant[i],
               stats->both_reads->k1_both_reads_not_threshold_by_contaminant_pc[i],
               stats->both_reads->k1_either_read_not_threshold_by_contaminant[i],
               stats->both_reads->k1_either_read_not_threshold_by_contaminant_pc[i],
               stats->both_reads->threshold_passed_reads_unique_by_contaminant[i],
               stats->both_reads->threshold_passed_reads_unique_by_contaminant_pc[i],
               stats->both_reads->k1_both_reads_not_threshold_unique_by_contaminant[i],
               stats->both_reads->k1_both_reads_not_threshold_unique_by_contaminant_pc[i],
               stats->both_reads->k1_either_read_not_threshold_unique_by_contaminant[i],
               stats->both_reads->k1_either_read_not_threshold_unique_by_contaminant_pc[i]);
    }
    
}

/*----------------------------------------------------------------------*
 * Function:
 * Purpose:
 * Parameters: None
 * Returns:    None
 *----------------------------------------------------------------------*/
void kmer_stats_report_to_screen(KmerStats* stats, CmdLine* cmd_line)
{
    int r;
    
    printf("\nThreshold: at least %d kmers in each read and at least %d in pair\n", cmd_line->kmer_threshold_read, cmd_line->kmer_threshold_overall);
    
    for (r=0; r<stats->number_of_files; r++) {
        printf("\n========== Statistics for Read %d ===========\n\n", r+1);
        kmer_stats_report_read_stats(stats, r, cmd_line);
    }

    printf("\n========== Key ==========\n\n");
    printf("nKmers    - Number of kmers in contaminant reference\n");
    printf("kFound    - Number of unique contaminant kmers found in reads\n");
    printf("%%kFound   - Percentage of contaminant kmers found in reads\n");
    printf("ReadsW1k  - Reads containing 1 or more kmer from the contaminant\n");
    printf("%%ReadsW1k - Percentage of reads containing 1 or more kmer from the contaminant\n");
    printf("UniqW1k   - Reads containing 1 or more kmer from the contaminant and not any other\n");
    printf("%%UniqW1k  - Percentage of reads containing 1 or more kmer from the contaminant and not any other\n");
    printf("ReadsWnk  - Reads containing n or more kmer from the contaminant (n=%d)\n", cmd_line->kmer_threshold_read);
    printf("%%ReadsWnk - Percentage of reads containing n or more kmer from the contaminant (n=%d)\n", cmd_line->kmer_threshold_read);
    printf("UniqWnk   - Reads containing n or more kmer from the contaminant and not any other (n=%d)\n", cmd_line->kmer_threshold_read);
    printf("%%UniqWnk  - Percentage of reads containing n or more kmer from the contaminant and not any other (n=%d)\n", cmd_line->kmer_threshold_read);
    printf("Assigned  - Reads assigned to this species\n");
    printf("%%Assigned - Percentage of reads assigned to this species\n");
    
    if (stats->number_of_files == 2) {
        printf("\n========== Statistics for both reads ===========\n\n");
        kmer_stats_report_both_stats(stats, cmd_line);
    }

    printf("\n========== Key ==========\n\n");
    printf("nKmers    - Number of kmers in contaminant reference\n");
    printf("kFound    - Number of unique contaminant kmers found in reads\n");
    printf("%%kFound   - Percentage of contaminant kmers found in reads\n");
    printf("ReadsThr  - Reads passing threshold\n");
    printf("%%ReadsThr - Percentage of reads passing threshold\n");
    printf("BothW1k   - Reads not passing threshold, but containing 1 or more kmer in both reads\n");
    printf("%%BothW1k  - Percentage of reads not passing threshold, but containing 1 or more kmer in both reads\n");
    printf("EithW1k   - Reads not passing threshold, but containing 1 or more kmer in either read\n");
    printf("%%EithW1k  - Percentage of reads not passing threshold, but containing 1 or more kmer in either read\n");
}

void check_kmers_in_common(Element* node, void* data) {
    KmerStats* stats = data;
    int i, j;
    for (i=0; i<(stats->n_contaminants); i++) {
        if (element_get_contaminant_bit(node, i) > 0) {
            for (j=i; j<stats->n_contaminants; j++) {
                if (element_get_contaminant_bit(node, j) > 0) {
                    stats->kmers_in_common[i][j]++;
                    if (i != j) {
                        stats->kmers_in_common[j][i]++;
                    }
                }
            }
        }
    }
}

void check_unique_kmers(Element* node, void* data) {
    KmerStats* stats = data;
    int i;
    int count = 0;
    int index = 0;
    
    for (i=0; i<(stats->n_contaminants); i++) {
        if (element_get_contaminant_bit(node, i) > 0) {
            index = i;
            count++;
            if (count > 1) {
                break;
            }
        }
    }
    
    if (count == 1) {
        stats->unique_kmers[index]++;
    }
}

/*----------------------------------------------------------------------*
 * Function:
 * Purpose:
 * Parameters: None
 * Returns:    None
 *----------------------------------------------------------------------*/
void kmer_stats_compare_contaminant_kmers(HashTable* hash, KmerStats* stats, CmdLine* cmd_line)
{
    int i, j;
    FILE* fp_abs;
    FILE* fp_pc;
    FILE* fp_abs_unique;
    FILE* fp_pc_unique;
    char* filename_abs;
    char* filename_pc;
    char* filename_pc_unique;
    char* filename_abs_unique;
    
    if (stats->n_contaminants < 2) {
        return;
    }
    
    printf("\nComparing contaminant kmers...\n");
    
    filename_abs = malloc(strlen(cmd_line->output_prefix)+32);
    if (!filename_abs) {
        printf("Error: No room to store filename!\n");
        exit(1);
    }
    sprintf(filename_abs, "%skmer_similarity_absolute.txt", cmd_line->output_prefix);

    filename_pc = malloc(strlen(cmd_line->output_prefix)+32);
    if (!filename_pc) {
        printf("Error: No room to store filename!\n");
        exit(1);
    }
    sprintf(filename_pc, "%skmer_similarity_pc.txt", cmd_line->output_prefix);

    filename_abs_unique = malloc(strlen(cmd_line->output_prefix)+32);
    if (!filename_abs_unique) {
        printf("Error: No room to store filename!\n");
        exit(1);
    }
    sprintf(filename_abs_unique, "%skmer_unique_absolute.txt", cmd_line->output_prefix);
    
    filename_pc_unique = malloc(strlen(cmd_line->output_prefix)+32);
    if (!filename_pc_unique) {
        printf("Error: No room to store filename!\n");
        exit(1);
    }
    sprintf(filename_pc_unique, "%skmer_unique_pc.txt", cmd_line->output_prefix);
    
    fp_abs = fopen(filename_abs, "w");
    if (!fp_abs) {
        printf("Error: can't open %s\n", filename_abs);
        exit(1);
    } else {
        printf("Opened %s\n", filename_abs);
    }

    fp_pc = fopen(filename_pc, "w");
    if (!fp_pc) {
        printf("Error: can't open %s\n", filename_pc);
        exit(1);
    } else {
        printf("Opened %s\n", filename_pc);
    }

    fp_abs_unique = fopen(filename_abs_unique, "w");
    if (!fp_abs_unique) {
        printf("Error: can't open %s\n", filename_abs_unique);
        exit(1);
    } else {
        printf("Opened %s\n", filename_abs_unique);
    }
    
    fp_pc_unique = fopen(filename_pc_unique, "w");
    if (!fp_pc_unique) {
        printf("Error: can't open %s\n", filename_pc_unique);
        exit(1);
    } else {
        printf("Opened %s\n", filename_pc_unique);
    }
    
    hash_table_traverse_with_data(&check_kmers_in_common, (void*)stats, hash);

    hash_table_traverse_with_data(&check_unique_kmers, (void*)stats, hash);
    
    printf("\n%15s ", "");
    fprintf(fp_abs, "Contaminant");
    fprintf(fp_pc, "Contaminant");
    for (i=0; i<stats->n_contaminants; i++) {
        printf(" %15s", stats->contaminant_ids[i]);
        fprintf(fp_abs, "\t%s", stats->contaminant_ids[i]);
        fprintf(fp_pc, "\t%s", stats->contaminant_ids[i]);
        fprintf(fp_abs_unique, "\t%s", stats->contaminant_ids[i]);
        fprintf(fp_pc_unique, "\t%s", stats->contaminant_ids[i]);
    }
    printf("\n");
    fprintf(fp_abs, "\n");
    fprintf(fp_pc, "\n");
    fprintf(fp_abs_unique, "\n");
    fprintf(fp_pc_unique, "\n");
    
    for (i=0; i<stats->n_contaminants; i++) {
        printf("%15s", stats->contaminant_ids[i]);
        fprintf(fp_abs, "%s", stats->contaminant_ids[i]);
        fprintf(fp_pc, "%s", stats->contaminant_ids[i]);
        for (j=0; j<stats->n_contaminants; j++) {
            double pc = 0;
            printf(" %15d", stats->kmers_in_common[i][j]);
            fprintf(fp_abs, "\t%d", stats->kmers_in_common[i][j]);

            if (stats->kmers_in_common[i][j] > 0) {
                pc = (100.0 * (double)stats->kmers_in_common[i][j]) / (double)stats->contaminant_kmers[i];
            }

            fprintf(fp_pc, "\t%.2f", pc);
        }
        printf("\n");
        fprintf(fp_abs, "\n");
        fprintf(fp_pc, "\n");
    }

    for (i=0; i<stats->n_contaminants; i++) {
        double pc = 0;
        
        if (stats->unique_kmers[i] > 0) {
            pc = (100.0 * (double)stats->unique_kmers[i]) / (double)stats->contaminant_kmers[i];
        }
        
        if (i > 0) {
            fprintf(fp_abs_unique, "\t");
            fprintf(fp_pc_unique, "\t");
        }
        fprintf(fp_abs_unique, "%d", stats->unique_kmers[i]);
        fprintf(fp_pc_unique, "%.2f", pc);
    }
    
    fclose(fp_abs_unique);
    fclose(fp_pc_unique);
    fclose(fp_abs);
    fclose(fp_pc);
}

void kmer_stats_write_progress(KmerStats* stats, CmdLine* cmd_line)
{
    char* filename;
    FILE* fp;
    int r;
    
    printf("Updating...\n");
    
    filename = malloc(strlen(cmd_line->progress_dir) + 64);
    if (!filename) {
        printf("Error: no room for filename\n");
        exit(1);
    }
    
    for (r=0; r<stats->number_of_files; r++) {
        sprintf(filename, "%s/data_overall_r%d.txt", cmd_line->progress_dir, r+1);
        fp = fopen(filename, "w");
        if (fp) {
            printf("Opening %s\n", filename);
            fprintf(fp, "name\tvalue\n");
            fprintf(fp, "Number of reads\t%d\n", stats->read[r]->number_of_reads);
            fprintf(fp, "Number with k1 contaminants\t%d\n", stats->read[r]->k1_contaminated_reads);
            fprintf(fp, "Number with k%d contaminants\t%d\n", cmd_line->kmer_threshold_read, stats->read[r]->kn_contaminated_reads);
            fclose(fp);
        } else {
            printf("Error: can't open %s\n", filename);
        }

        sprintf(filename, "%s/data_per_contaminant_r%d.txt", cmd_line->progress_dir, r+1);
        fp = fopen(filename, "w");
        if (fp) {
            int i;
            printf("Opening %s\n", filename);
            fprintf(fp, "name\tvalue\n");
            for (i=0; i<stats->n_contaminants; i++) {
                fprintf(fp, "%s\t%d\n", stats->contaminant_ids[i], stats->read[r]->kn_contaminated_reads_by_contaminant[i]);
            }
            fclose(fp);
        } else {
            printf("Error: can't open %s\n", filename);
        }

        sprintf(filename, "%s/largest_contaminant_r%d.txt", cmd_line->progress_dir, r+1);
        fp = fopen(filename, "w");
        if (fp) {
            int i;
            printf("Opening %s\n", filename);
            fprintf(fp, "name\tvalue\n");
            for (i=0; i<stats->n_contaminants; i++) {
                fprintf(fp, "%s\t%d\n", stats->contaminant_ids[i], stats->read[r]->reads_with_highest_contaminant[i]);
            }
            fprintf(fp, "Unclassified\t%d\n", stats->read[r]->reads_unclassified);
            fclose(fp);
        } else {
            printf("Error: can't open %s\n", filename);
        }
    
    }
}
