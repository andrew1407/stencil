package validate

// Per-field caps on project metadata and the hello name. A field rides every feed event, Redis publish
// and list page, so each is bounded on its own rather than by the body cap alone.

import (
	"errors"
	"regexp"
	"strconv"
	"unicode/utf8"

	"stencil/server/internal/protocol"
)

const (
	// MaxNameChars is the browser's validateName and the core's ProjectsStore::MAX_NAME_LENGTH.
	MaxNameChars = 80
	// MaxDescriptionChars bounds the free-text description.
	MaxDescriptionChars = 2000
	// MaxURLChars bounds source and resource, the provenance URLs.
	MaxURLChars = 2048
	// MaxKeywords and MaxKeywordChars bound the search keywords, count and each.
	MaxKeywords     = 32
	MaxKeywordChars = 64
)

// colorPattern is the one colour shape the clients write: "#rrggbb".
var colorPattern = regexp.MustCompile(`^#[0-9a-fA-F]{6}$`)

// Name refuses a project or hello name past MaxNameChars; empty is allowed (the server names it).
func Name(name string) error { return capped("name", name, MaxNameChars) }

// CreateProject refuses a create whose metadata overruns a field cap.
func CreateProject(req protocol.CreateProjectRequest) error {
	return firstErr(
		Name(req.Name),
		capped("description", req.Description, MaxDescriptionChars),
		capped("source", req.Source, MaxURLChars),
		capped("resource", req.Resource, MaxURLChars),
		color("color", req.Color),
		color("blankColor", req.BlankColor),
		keywords(req.Keywords),
	)
}

// UpdateProject refuses an update whose named fields overrun a cap; an absent field is unchanged.
func UpdateProject(req protocol.UpdateProjectRequest) error {
	errs := []error{Version(req.Version)}
	if req.Name != nil {
		errs = append(errs, Name(*req.Name))
	}
	if req.Description != nil {
		errs = append(errs, capped("description", *req.Description, MaxDescriptionChars))
	}
	if req.Color != nil {
		errs = append(errs, color("color", *req.Color))
	}
	if req.BlankColor != nil {
		errs = append(errs, color("blankColor", *req.BlankColor))
	}
	if req.Keywords != nil {
		errs = append(errs, keywords(*req.Keywords))
	}
	return firstErr(errs...)
}

func capped(field, value string, maxChars int) error {
	if utf8.RuneCountInString(value) > maxChars {
		return errors.New(field + " must be at most " + strconv.Itoa(maxChars) + " characters")
	}
	return nil
}

func color(field, value string) error {
	if value != "" && !colorPattern.MatchString(value) {
		return errors.New(field + " must be #rrggbb")
	}
	return nil
}

func keywords(list []string) error {
	if len(list) > MaxKeywords {
		return errors.New("keywords must be at most " + strconv.Itoa(MaxKeywords) + " entries")
	}
	for _, k := range list {
		if err := capped("keyword", k, MaxKeywordChars); err != nil {
			return err
		}
	}
	return nil
}

func firstErr(errs ...error) error {
	for _, err := range errs {
		if err != nil {
			return err
		}
	}
	return nil
}
