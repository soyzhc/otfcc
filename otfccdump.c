#include "caryll-sfnt.h"
#include "caryll-font.h"
#include <unistd.h>
#include <getopt.h>
#include "extern/json-builder.h"
#include "support/stopwatch.h"
#include "version.h"

int main(int argc, char *argv[]) {
	bool show_help = false;
	bool show_version = false;
	bool show_pretty = false;
	bool show_ugly = false;
	bool show_time = false;
	uint32_t ttcindex = 0;
	struct option longopts[] = {{"version", no_argument, NULL, 'v'},
	                            {"help", no_argument, NULL, 'h'},
	                            {"pretty", no_argument, NULL, 'p'},
	                            {"ugly", no_argument, NULL, 0},
	                            {"time", no_argument, NULL, 0},
	                            {"ignore-glyph-order", no_argument, NULL, 0},
	                            {"ignore-hints", no_argument, NULL, 0},
	                            {"output", required_argument, NULL, 'o'},
	                            {"ttc-index", required_argument, NULL, 'n'},
	                            {0, 0, 0, 0}};
	caryll_dump_options dumpopts = {.ignore_glyph_order = false, .ignore_hints = false};
	int option_index = 0;
	int c;

	sds outputPath = NULL;
	sds inPath = NULL;

	while ((c = getopt_long(argc, argv, "vhpo:n:", longopts, &option_index)) != (-1)) {
		switch (c) {
		case 0:
			/* If this option set a flag, do nothing else now. */
			if (longopts[option_index].flag != 0) break;
			if (strcmp(longopts[option_index].name, "ugly") == 0) { show_ugly = true; }
			if (strcmp(longopts[option_index].name, "time") == 0) { show_time = true; }
			if (strcmp(longopts[option_index].name, "ignore-glyph-order") == 0) { dumpopts.ignore_glyph_order = true; }
			if (strcmp(longopts[option_index].name, "ignore-hints") == 0) { dumpopts.ignore_hints = true; }
			break;
		case 'v':
			show_version = true;
			break;
		case 'h':
			show_help = true;
			break;
		case 'p':
			show_pretty = true;
			break;
		case 'o':
			outputPath = sdsnew(optarg);
			break;
		case 'n':
			ttcindex = atoi(optarg);
			break;
		}
	}
	if (show_help) {
		fprintf(stdout, "This is otfccdump, version %s.\n", VERSION);
		fprintf(stdout, "\n"
		                "Usage : otfccdump [OPTIONS] input.[otf|ttf|ttc]\n\n"
		                " -h, --help              : Display this help message and exit.\n"
		                " -v, --version           : Display version information and exit.\n"
		                " -o <file>               : Set output file path to <file>.\n"
		                " -n <n>, --ttc-index <n> : Use the <n>th subfont within the input font file.\n"
		                " --pretty                : Prettify the output JSON.\n"
		                " --ugly                  : Force uglify the output JSON.\n"
		                " --time                  : Time each substep.\n"
		                " --ignore-glyph-order    : Do not export glyph order information.\n"
		                " --ignore-hints          : Do not export hingint information.\n");
		return 0;
	}
	if (show_version) {
		fprintf(stdout, "This is otfccdump, version %s.\n", VERSION);
		return 0;
	}

	if (optind >= argc) {
		fprintf(stderr, "Expected argument after options\n");
		exit(EXIT_FAILURE);
	} else {
		inPath = sdsnew(argv[optind]);
	}

	struct timespec begin;

	time_now(&begin);

	caryll_sfnt *sfnt;
	{
		sfnt = caryll_sfnt_open(inPath);
		if (!sfnt || sfnt->count == 0) {
			fprintf(stderr, "Cannot read SFNT file \"%s\". Exit.\n", inPath);
			exit(EXIT_FAILURE);
		}
		if (ttcindex >= sfnt->count) {
			fprintf(stderr, "Subfont index %d out of range for \"%s\" (0 -- %d). Exit.\n", ttcindex, inPath,
			        (sfnt->count - 1));
			exit(EXIT_FAILURE);
		}
	}

	caryll_font *font;
	{
		font = caryll_font_open(sfnt, ttcindex);
		if (show_time) push_stopwatch("Parse SFNT", &begin);
	}

	json_value *root;
	{
		root = json_object_new(12);
		caryll_head_to_json(font->head, root, dumpopts);
		caryll_hhea_to_json(font->hhea, root, dumpopts);
		caryll_maxp_to_json(font->maxp, root, dumpopts);
		caryll_name_to_json(font->name, root, dumpopts);
		caryll_post_to_json(font->post, root, dumpopts);
		caryll_OS_2_to_json(font->OS_2, root, dumpopts);
		caryll_cmap_to_json(font->cmap, root, dumpopts);
		caryll_glyf_to_json(font->glyf, root, dumpopts);
		if (!dumpopts.ignore_hints) {
			caryll_fpgm_prep_to_json(font->fpgm, root, dumpopts, "fpgm");
			caryll_fpgm_prep_to_json(font->prep, root, dumpopts, "prep");
			caryll_fpgm_prep_to_json(font->cvt_, root, dumpopts, "cvt_");
		}
		if (show_time) push_stopwatch("Convert to JSON", &begin);
	}

	char *buf;
	{
		json_serialize_opts options;
		options.mode = json_serialize_mode_packed;
		options.opts = 0;
		options.indent_size = 4;
		if (show_pretty || (!outputPath && isatty(fileno(stdout)))) { options.mode = json_serialize_mode_multiline; }
		if (show_ugly) options.mode = json_serialize_mode_packed;
		buf = malloc(json_measure_ex(root, options));
		json_serialize_ex(buf, root, options);
		if (show_time) push_stopwatch("Serialize to string", &begin);
	}

	{
		if (outputPath) {
			FILE *outputFile = fopen(outputPath, "wb");
			if (!outputFile) {
				fprintf(stderr, "Cannot write to file \"%s\". Exit.", outputPath);
				exit(EXIT_FAILURE);
			}
			fputs(buf, outputFile);
			fclose(outputFile);
		} else {
			fputs(buf, stdout);
		}
		if (show_time) push_stopwatch("Write to file", &begin);
	}

	{
		free(buf);
		json_builder_free(root);
		caryll_font_close(font);
		caryll_sfnt_close(sfnt);
		if (inPath) sdsfree(inPath);
		if (outputPath) sdsfree(outputPath);
		if (show_time) push_stopwatch("Complete", &begin);
	}

	return 0;
}
