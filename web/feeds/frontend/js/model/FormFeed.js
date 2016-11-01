/**
 * Created by vrushali on 15/10/15.
 */
define('FormFeed', ["jquery", "Feed"], function ($) {
    var Feed = require('Feed');
    var FormFeed = function (feed_name, feed_path, config) {
        this.base = Feed;
        this.base(feed_name, feed_path, config);
        this.fields = [];
        this.form = $('<form>');
        this.extract_fields(config.form);
        // this.directory = this.config.form.directory != undefined ? this.config.form.directory : "/logging";

    };

    FormFeed.prototype = Object.create(Feed.prototype);

    FormFeed.prototype.extract_fields = function (form_elements) {
        if (form_elements != undefined) {
            this.populate_path_values(form_elements, "");
        }
        console.log(this.fields);
    };


    FormFeed.prototype.populate_path_values = function (form_elements, prefix) {
        for (var element in form_elements) {
            var value = form_elements[element];
            var type = typeof value;
            if (type == "object") {
                var p = get_prefix(element, prefix);
                this.populate_path_values(value, p);
            }
            else if (type = "string") {
                var p = get_prefix(element, prefix);
                this.fields[p] = value;
            }
            // console.log(element + " typeof " + (typeof element) + "   type= " + type);
        }
    };

    var get_prefix = function (key, prefix) {
        var p;
        if (prefix.length > 0) {
            var type = typeof prefix;
            if (isNaN(key)) {
                p = prefix + "/" + key;
            }
            else {
                p = prefix + "[" + key + "]";
            }
        }
        else {
            p = key;
        }
        return p;
    };

    FormFeed.prototype.init = function () {
        this.target.empty();
        var url = this.get_url();

        for (var field in this.fields) {
            var row = $('<div>', {class: "form-group"});
            var label = $('<label>',
                {
                    text: field,
                    class: "col-sm-5"
                });
            var input = $('<input>',
                {
                    type: 'text',
                    class: "col-sm-6",
                    name: field,
                    value: this.fields[field]
                });
            row.append(label).append(input);
            this.form.append(row);
        }
        // var combo = $('<select>');
        //
        // $("form").submit(function (event) {
        //     alert("Handler for .submit() called.");
        //     event.preventDefault();
        // });
        var buttons = $('<div>', {class: "form-group"});
        var this_ = this;

        var submit = $('<input/>',
            {
                value: 'Submit',
                class: "btn btn-primary col-sm-3",
                click: function (event) {
                    event.preventDefault();
                    if (url != undefined) {
                        $.ajax({
                            // context: this,
                            data: $(this).closest("form").serialize(),
                            url: url
                        }).done(function (data, textStatus, jqXHR) {
                            // FormFeed.prototype.onload_(data);
                            this_.onload(data);
                        }).fail(function (jqXHR, textStatus, errorThrown) {
                            FormFeed.prototype.onerror();
                        });
                    }
                }
            });

        var clear = $('<button/>',
            {
                text: 'Clear',
                type: 'button',
                class: "btn btn-default col-sm-3",
                click: function () {
                    $($(this).closest("form").find("input")).each(function () {
                        $(this).val('');
                    });
                }
            });
        var space = $('<label/>',
            {class: "col-sm-1"});
        // form.append(combo).append("<br>");
        // form.append(input).append("<br>");
        buttons.append(submit).append(space).append(clear);
        this.form.append(buttons);
        this.target.append(this.form);
        if (this.fields.length > 0) {
            this.target.width(500);
        }
        else {
            $(this.target).css("min-height", function () {
                return 80;
            });
            $(this.target).css("min-width", function () {
                return 200;
            });
        }
    };

    FormFeed.prototype.load = function () {
        this.init();

        // $.ajax({
        //     context: this,
        //     url: this.get_url()
        // }).done(function (data, textStatus, jqXHR) {
        //     this.onload(data);
        // }).fail(function (jqXHR, textStatus, errorThrown) {
        //     this.onerror();
        // });
    };

    FormFeed.prototype.onload_ = function (data) {
        // this.target.height(100);
        // data = data.replace(/\n/g, '<br/>');
        $(this.form).parent().find(".result-panel").remove();
        var panel = $('<div>', {class: "panel result-panel col-sm-11"});
        panel.append($('<span>', {class: "label label-info", text: "Output"}));
        panel.append($('<div>', {class: "form-results", text: data}));
        $(this.form).append(panel)
    };
    return FormFeed;
});
